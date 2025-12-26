//
#include "CentralizedServer.h"
#include <iostream>
#include <chrono>

CCentralizedServer::CCentralizedServer(int port, int maxClients, int mainlogicTickMs)
    : _networkServer(std::make_shared<CIOCPServer>(port, maxClients, ServerArchitectureType::Centralized))
    , _running(false)
    , _mainlogicTickMs(mainlogicTickMs)
{
    if (!_networkServer->Initialize())
    {
        std::cerr << "[CentralizedServer] Failed to initialize network server" << std::endl;
        throw std::runtime_error("Network server initialization failed");
    }
}

CCentralizedServer::~CCentralizedServer()
{
    Stop();
    _networkServer->Disconnect();
}

void CCentralizedServer::Start()
{
    _running = true;
	_networkServer->Start(); // 네트워크 서버 시작
    _gameThread = std::thread(&CCentralizedServer::GameLogicThread, this);
    std::cout << "[CentralizedServer] Game logic thread started" << std::endl;
}

void CCentralizedServer::Stop()
{
    if (!_running)
    {
        return;
    }

    _running = false;

    if (_gameThread.joinable())
    {
        _gameThread.join();
    }

    std::cout << "[CentralizedServer] Game logic thread stopped" << std::endl;
}

void CCentralizedServer::GameLogicThread()
{
    while (_running)
    {
        // 네트워크 이벤트 처리
        NetworkEvent event(NetworkEvent::Type::CONNECTED, -1);
        while (_networkServer->PopNetworkEvent(event))
        {
            switch (event.type)
            {
            case NetworkEvent::Type::CONNECTED:
                DispatchClientConnected(event.sessionId);
                break;
            case NetworkEvent::Type::DISCONNECTED:
                DispatchClientDisconnected(event.sessionId);
                break;
            case NetworkEvent::Type::RECEIVED:
                DispatchDataReceived(event.sessionId, event.data.data(), event.data.size());
                break;
            }
        }

        // 게임 로직 처리
        ProcessGameLogic();

        // CPU 부하 방지 (설정값 사용)
        if (_mainlogicTickMs >= 0)
        {   
            std::this_thread::sleep_for(std::chrono::milliseconds(_mainlogicTickMs));
        }
    }
}

void CCentralizedServer::DispatchClientConnected(int64_t sessionId)
{
    std::cout << "[CentralizedServer] Client connected - SessionId: " << sessionId << std::endl;

    // 환영 메시지 전송 예시
    std::string welcomeMsg = "Welcome to the centralized game server!";
    _networkServer->RequestSendPacket(sessionId, welcomeMsg.c_str(), static_cast<int>(welcomeMsg.size()));
}

void CCentralizedServer::DispatchClientDisconnected(int64_t sessionId)
{
    std::cout << "[CentralizedServer] Client disconnected - SessionId: " << sessionId << std::endl;

    // 세션 정리 로직 추가 가능
}

void CCentralizedServer::DispatchDataReceived(int64_t sessionId, const char* data, size_t length)
{
    std::cout << "[CentralizedServer] Data received from SessionId: " << sessionId 
              << ", Length: " << length << std::endl;

    // 게임 패킷 파싱 및 처리
    // 예시: 에코백
    _networkServer->RequestSendPacket(sessionId, data, static_cast<int>(length));

    // 브로드캐스트 예시
    // _networkServer->RequestBroadcastPacket(data, static_cast<int>(length));
}

void CCentralizedServer::ProcessGameLogic()
{
    // 여기에 게임 로직 추가
    // 예: 방 관리, 매치메이킹, 게임 상태 업데이트 등
}