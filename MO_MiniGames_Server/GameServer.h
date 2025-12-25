#pragma once

#include "IOCPServer.h"
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>

// 게임 로직 레이어 - 별도 스레드에서 동작
class CGameServer
{
public:
    explicit CGameServer(std::shared_ptr<CIOCPServer> networkServer, int logicTickMs = -1);
    virtual ~CGameServer();

    void Start();
    void Stop();

private:
    void GameLogicThread();

    // 네트워크 이벤트 처리
    void OnClientConnected(int64_t sessionId);
    void OnClientDisconnected(int64_t sessionId);
    void OnDataReceived(int64_t sessionId, const char* data, size_t length);

    // 게임 로직 (예시)
    void ProcessGameLogic();

private:
    std::shared_ptr<CIOCPServer> _networkServer;
    std::thread _gameThread;
    std::atomic<bool> _running;
    int _mainlogicTickMs;
};