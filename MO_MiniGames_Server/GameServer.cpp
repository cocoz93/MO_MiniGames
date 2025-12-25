#include "GameServer.h"
#include <iostream>
#include <chrono>

CGameServer::CGameServer(std::shared_ptr<CIOCPServer> networkServer, int mainlogicTickMs)
    : _networkServer(networkServer)
    , _running(false)
    , _mainlogicTickMs(mainlogicTickMs)
{
}

CGameServer::~CGameServer()
{
    Stop();
}

void CGameServer::Start()
{
    _running = true;
    _gameThread = std::thread(&CGameServer::GameLogicThread, this);
    std::cout << "[GameServer] Game logic thread started" << std::endl;
}

void CGameServer::Stop()
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

    std::cout << "[GameServer] Game logic thread stopped" << std::endl;
}

void CGameServer::GameLogicThread()
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
                OnClientConnected(event.sessionId);
                break;
            case NetworkEvent::Type::DISCONNECTED:
                OnClientDisconnected(event.sessionId);
                break;
            case NetworkEvent::Type::RECEIVED:
                OnDataReceived(event.sessionId, event.data.data(), event.data.size());
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

void CGameServer::OnClientConnected(int64_t sessionId)
{
    std::cout << "[GameServer] Client connected - SessionId: " << sessionId << std::endl;

    // 환영 메시지 전송 예시
    std::string welcomeMsg = "Welcome to the game server!";
    _networkServer->RequestSendPacket(sessionId, welcomeMsg.c_str(), static_cast<int>(welcomeMsg.size()));
}

void CGameServer::OnClientDisconnected(int64_t sessionId)
{
    std::cout << "[GameServer] Client disconnected - SessionId: " << sessionId << std::endl;

    // 세션 정리 로직 추가 가능
}

void CGameServer::OnDataReceived(int64_t sessionId, const char* data, size_t length)
{
    std::cout << "[GameServer] Data received from SessionId: " << sessionId 
              << ", Length: " << length << std::endl;

    // 게임 패킷 파싱 및 처리
    // 예시: 에코백
    _networkServer->RequestSendPacket(sessionId, data, static_cast<int>(length));

    // 브로드캐스트 예시
    // _networkServer->RequestBroadcastPacket(data, static_cast<int>(length));
}

void CGameServer::ProcessGameLogic()
{
    // 여기에 게임 로직 추가
    // 예: 방 관리, 매치메이킹, 게임 상태 업데이트 등
}