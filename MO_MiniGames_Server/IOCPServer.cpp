#include "IOCPServer.h"
#include <iostream>

extern void SignalProcessShutdown(); // main쪽에 정의된 함수

// CSession Implementation
CSession::CSession(SOCKET socket, int64_t sessionId)
    : _socket(socket)
    , _sessionId(sessionId)
    , _connected(true)
{
    _recvOverlapped.operation = IOOperation::RECV;
    _sendOverlapped.operation = IOOperation::SEND;
}

CSession::~CSession()
{
    Close();
}

SOCKET CSession::GetSocket() const
{
    return _socket;
}

int64_t CSession::GetSessionId() const
{
    return _sessionId;
}

bool CSession::IsConnected() const
{
    return _connected;
}

void CSession::SetConnected(bool connected)
{
    _connected = connected;
}

OverlappedEx* CSession::GetRecvOverlapped()
{
    return &_recvOverlapped;
}

OverlappedEx* CSession::GetSendOverlapped()
{
    return &_sendOverlapped;
}

void CSession::Close()
{
    if (_socket != INVALID_SOCKET)
    {
        closesocket(_socket);
        _socket = INVALID_SOCKET;
    }
    _connected = false;
}

// CIOCPServer Implementation
CIOCPServer::CIOCPServer(int port, int maxClients, ServerArchitectureType type)
    : _port(port)
    , _maxClients(maxClients)
    , _architectureType(type)
    , _running(false)
    , _sessionIdCounter(0)
    , _listenSocket(INVALID_SOCKET)
    , _iocpHandle(NULL)
{
}

CIOCPServer::~CIOCPServer()
{
    Disconnect();
    SignalProcessShutdown(); //메인 스레드 종료
}

bool CIOCPServer::Initialize()
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "WSAStartup failed" << std::endl;
        return false;
    }

    // IOCP 핸들 생성
    _iocpHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (_iocpHandle == NULL)
    {
        std::cerr << "CreateIoCompletionPort failed" << std::endl;
        WSACleanup();
        return false;
    }

    // Listen 소켓 생성
    if (!CreateListenSocket())
    {
        CloseHandle(_iocpHandle);
        WSACleanup();
        return false;
    }

    return true;
}

bool CIOCPServer::CreateListenSocket()
{
    _listenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (_listenSocket == INVALID_SOCKET)
    {
        std::cerr << "WSASocket failed: " << WSAGetLastError() << std::endl;
        return false;
    }

    SOCKADDR_IN serverAddr;
    ZeroMemory(&serverAddr, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(_port);

    if (bind(_listenSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
    {
        std::cerr << "bind failed: " << WSAGetLastError() << std::endl;
        closesocket(_listenSocket);
        return false;
    }

    if (listen(_listenSocket, SOMAXCONN) == SOCKET_ERROR)
    {
        std::cerr << "listen failed: " << WSAGetLastError() << std::endl;
        closesocket(_listenSocket);
        return false;
    }

    return true;
}

bool CIOCPServer::BindIOCP(SOCKET socket, ULONG_PTR completionKey)
{
    auto handle = CreateIoCompletionPort((HANDLE)socket, _iocpHandle, completionKey, 0);
    if (handle == NULL)
    {
        std::cerr << "BindIOCP failed: " << GetLastError() << std::endl;
        return false;
    }
    return true;
}

void CIOCPServer::Start()
{
    _running = true;

    // 워커 스레드 생성 (CPU 코어 * 2)
    int threadCount = std::thread::hardware_concurrency() * 2;
    for (int i = 0; i < threadCount; ++i)
    {
        _workerThreads.emplace_back(&CIOCPServer::WorkerThread, this);
    }

    // Accept 스레드 생성
    _acceptThread = std::thread(&CIOCPServer::AcceptThread, this);

    // 컨텐츠 스레드 생성 (Centralized 모드에만 필요)
    if (_architectureType == ServerArchitectureType::Centralized)
    {
        _commandThread = std::thread(&CIOCPServer::CommandProcessThread, this);
    }

    std::cout << "[Network] Server started with " << threadCount << " worker threads (Mode: ";
    
    switch (_architectureType) 
    {
    case ServerArchitectureType::Centralized: std::cout << "Centralized"; break;
    case ServerArchitectureType::Partitioned: std::cout << "Partitioned"; break;
    case ServerArchitectureType::UnifiedStrand: std::cout << "UnifiedStrand"; break;
    }
    std::cout << ")" << std::endl;
}

// 즉시 RST 전송(강제 종료)
void CIOCPServer::Disconnect()
{
    if (!_running)
    {
        return;
    }

    _running = false;

    // 모든 세션 강제 종료 (SO_LINGER{on,0} -> abortive close (RST))
    {
        std::lock_guard<std::mutex> lock(_sessionMutex);
        LINGER lingerOpt;
        lingerOpt.l_onoff = 1;   // linger 활성화
        lingerOpt.l_linger = 0;  // 0초 -> RST 전송

        for (auto& pair : _sessions)
        {
            auto session = pair.second;
            SOCKET s = session->GetSocket();
            if (s != INVALID_SOCKET)
            {
                // 실패해도 계속 진행
                setsockopt(s, SOL_SOCKET, SO_LINGER, reinterpret_cast<char*>(&lingerOpt), sizeof(lingerOpt));
                // 직접 closesocket 호출하여 RST 발생시키기
                closesocket(s);
            }
            session->SetConnected(false);
        }
        _sessions.clear();
    }

    // Listen 소켓도 닫음 (정상적으로 닫아도 무방)
    if (_listenSocket != INVALID_SOCKET)
    {
        closesocket(_listenSocket);
        _listenSocket = INVALID_SOCKET;
    }

    // IOCP 워커 스레드 깨우기
    if (_iocpHandle != NULL)
    {
        for (size_t i = 0; i < _workerThreads.size(); ++i)
        {
            PostQueuedCompletionStatus(_iocpHandle, 0, 0, nullptr);
        }
    }

    // 스레드 종료 대기
    for (auto& thread : _workerThreads)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }

    if (_acceptThread.joinable())
    {
        _acceptThread.join();
    }

    if (_commandThread.joinable())
    {
        _commandThread.join();
    }

    if (_iocpHandle != NULL)
    {
        CloseHandle(_iocpHandle);
        _iocpHandle = NULL;
    }

    WSACleanup();
}

void CIOCPServer::AcceptThread()
{
    while (_running)
    {
        SOCKADDR_IN clientAddr;
        int addrLen = sizeof(clientAddr);

        SOCKET clientSocket = accept(_listenSocket, (SOCKADDR*)&clientAddr, &addrLen);

        if (clientSocket == INVALID_SOCKET)
        {
            if (_running)
            {
                std::cerr << "accept failed: " << WSAGetLastError() << std::endl;
            }
            continue;
        }

        ProcessAccept(clientSocket);
    }
}

void CIOCPServer::ProcessAccept(SOCKET clientSocket)
{
    int64_t sessionId = ++_sessionIdCounter;
    auto session = std::make_shared<CSession>(clientSocket, sessionId);
    
	// IOCP에 클라이언트 소켓 바인딩
    if (!BindIOCP(clientSocket, (ULONG_PTR)session.get()))
    {
        std::cerr << "Failed to bind client socket to IOCP" << std::endl;
        session->Close();
        return;
    }

    // 세션 추가
    AddSession(session);



    // 컨텐츠쪽 전달 
    switch (_architectureType)
    {
    case ServerArchitectureType::Centralized: // 큐에 넣어서 별도 스레드로 전달
        PushNetworkEvent(NetworkEvent(NetworkEvent::Type::CONNECTED, sessionId));
		break;

    case ServerArchitectureType::UnifiedStrand: // 직접 처리 (하위 클래스에서 오버라이드된 메서드 호출)
        OnClientConnected(sessionId);
		break;

	default:
        break;
    }



    std::cout << "Client connected - SessionId: " << sessionId << std::endl;

    // 첫 Recv 요청
    DWORD flags = 0;
    DWORD recvBytes = 0;
    auto recvOverlapped = session->GetRecvOverlapped();

    int result = WSARecv(clientSocket, &recvOverlapped->wsaBuf, 1, &recvBytes, &flags,
        &recvOverlapped->overlapped, NULL);

    if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
    {
        std::cerr << "WSARecv failed: " << WSAGetLastError() << std::endl;
        RequestDisconnectSession(sessionId);
    }
}

void CIOCPServer::WorkerThread()
{
    while (_running)
    {
        DWORD bytesTransferred = 0;
        ULONG_PTR completionKey = 0;
        OVERLAPPED* overlapped = nullptr;

        BOOL result = GetQueuedCompletionStatus(_iocpHandle, &bytesTransferred,
            &completionKey, &overlapped, INFINITE);

        if (!_running)
            break;

		// 에러 또는 연결 종료 (에러와 연결종료 상황을 분류하지 않음)
        if (result == FALSE || bytesTransferred == 0)
        {
            if (overlapped != nullptr)
            {
                auto session = reinterpret_cast<CSession*>(completionKey);
                if (session)
                {
                    RequestDisconnectSession(session->GetSessionId());
                }
            }
            continue;
        }

		// 잘못된 completion key
        auto session = reinterpret_cast<CSession*>(completionKey);
        if (!session || !session->IsConnected())
        {
            continue;
        }

        auto overlappedEx = reinterpret_cast<OverlappedEx*>(overlapped);

        switch (overlappedEx->operation)
        {
        case IOOperation::RECV:
            ProcessRecv(session, bytesTransferred);
            break;
        case IOOperation::SEND:
            ProcessSend(session, bytesTransferred);
            break;
        default:
            break;  
        }
    }
}

// Recv 완료 통지
void CIOCPServer::ProcessRecv(CSession* session, DWORD bytesTransferred)
{
    auto recvOverlapped = session->GetRecvOverlapped();

    std::cout << "Received " << bytesTransferred << " bytes from SessionId: "
        << session->GetSessionId() << std::endl;


    // 컨텐츠쪽 전달
    switch (_architectureType)
    {
        case ServerArchitectureType::Centralized: // 큐에 넣어서 별도 스레드로 전달
            PushNetworkEvent(NetworkEvent(NetworkEvent::Type::RECEIVED, 
                session->GetSessionId(), recvOverlapped->buffer, bytesTransferred));
			break;

		case ServerArchitectureType::UnifiedStrand: // 직접 처리 (하위 클래스에서 오버라이드된 메서드 호출)
            OnDataReceived(session->GetSessionId(), recvOverlapped->buffer, bytesTransferred);
            break;

        default: 
            break;
    }


    // 다음 Recv 요청
    ZeroMemory(&recvOverlapped->overlapped, sizeof(OVERLAPPED));
    ZeroMemory(recvOverlapped->buffer, BUFFER_SIZE);
    recvOverlapped->wsaBuf.buf = recvOverlapped->buffer;
    recvOverlapped->wsaBuf.len = BUFFER_SIZE;

    DWORD flags = 0;
    DWORD recvBytes = 0;

    int result = WSARecv(session->GetSocket(), &recvOverlapped->wsaBuf, 1, &recvBytes, &flags,
        &recvOverlapped->overlapped, NULL);

    if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
    {
        RequestDisconnectSession(session->GetSessionId());
    }
}

// Send 완료 통지
void CIOCPServer::ProcessSend(CSession* session, DWORD bytesTransferred)
{
    // Send 완료 처리
	// 할일 없음
}

void CIOCPServer::RequestSendPacket(int64_t sessionId, const char* data, int length)
{
    auto session = GetSession(sessionId);
    if (!session || !session->IsConnected())
    {
        return;
    }

    auto sendOverlapped = session->GetSendOverlapped();
    ZeroMemory(&sendOverlapped->overlapped, sizeof(OVERLAPPED));

    memcpy(sendOverlapped->buffer, data, length);
    sendOverlapped->wsaBuf.buf = sendOverlapped->buffer;
    sendOverlapped->wsaBuf.len = length;

    DWORD sendBytes = 0;
    int result = WSASend(session->GetSocket(), &sendOverlapped->wsaBuf, 1, &sendBytes, 0,
        &sendOverlapped->overlapped, NULL);

    if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
    {
        RequestDisconnectSession(sessionId);
    }
}

void CIOCPServer::RequestBroadcastPacket(const char* data, int length)
{
    std::lock_guard<std::mutex> lock(_sessionMutex);
    for (auto& pair : _sessions)
    {
        RequestSendPacket(pair.first, data, length);
    }
}

void CIOCPServer::RequestDisconnectSession(int64_t sessionId)
{
    auto session = GetSession(sessionId);
    if (session)
    {
		// 컨텐츠 쪽 전달
        if (_architectureType == ServerArchitectureType::Centralized)
        {
            PushNetworkEvent(NetworkEvent(NetworkEvent::Type::DISCONNECTED, sessionId));
        }
        else // DIRECT_LOGIC
        {
            OnClientDisconnected(sessionId);
        }

        session->Close();
        RemoveSession(sessionId);
        std::cout << "Client disconnected - SessionId: " << sessionId << std::endl;
    }
}

std::shared_ptr<CSession> CIOCPServer::GetSession(int64_t sessionId)
{
    std::lock_guard<std::mutex> lock(_sessionMutex);
    auto it = _sessions.find(sessionId);
    return (it != _sessions.end()) ? it->second : nullptr;
}

void CIOCPServer::AddSession(std::shared_ptr<CSession> session)
{
    std::lock_guard<std::mutex> lock(_sessionMutex);
    _sessions[session->GetSessionId()] = session;
}

void CIOCPServer::RemoveSession(int64_t sessionId)
{
    std::lock_guard<std::mutex> lock(_sessionMutex);
    _sessions.erase(sessionId);
}

void CIOCPServer::PushNetworkEvent(NetworkEvent&& event)
{
    _eventQueue.Push(std::move(event));
}

bool CIOCPServer::PopNetworkEvent(NetworkEvent& event)
{
    return _eventQueue.TryPop(event);
}

void CIOCPServer::CommandProcessThread()
{
    while (_running)
    {
        NetworkCommand cmd(NetworkCommand::Type::SEND_PACKET, -1);
        if (_commandQueue.TryPop(cmd))
        {
            switch (cmd.type)
            {
            case NetworkCommand::Type::SEND_PACKET:
                RequestSendPacket(cmd.sessionId, cmd.data.data(), static_cast<int>(cmd.data.size()));
                break;
            case NetworkCommand::Type::DISCONNECT_SESSION:
                RequestDisconnectSession(cmd.sessionId);
                break;
            case NetworkCommand::Type::BROADCAST_PACKET:
                RequestBroadcastPacket(cmd.data.data(), static_cast<int>(cmd.data.size()));
                break;
            }
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}


// 다이렉트 모드용 기본 구현 (하위 클래스에서 오버라이드 가능)
// (UnifiedStrand)
void CIOCPServer::OnClientConnected(int64_t sessionId)
{
    // 기본 동작: 에코 테스트용 환영 메시지
    std::string welcomeMsg = "Welcome! (Direct mode)";
    RequestSendPacket(sessionId, welcomeMsg.c_str(), static_cast<int>(welcomeMsg.size()));
}

void CIOCPServer::OnClientDisconnected(int64_t sessionId)
{
    // 기본 동작: 아무것도 하지 않음
}

void CIOCPServer::OnDataReceived(int64_t sessionId, const char* data, size_t length)
{
    // 기본 동작: 에코백
    RequestSendPacket(sessionId, data, static_cast<int>(length));
}