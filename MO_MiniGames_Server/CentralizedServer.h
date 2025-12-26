#pragma once

#include "IOCPServer.h"
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>

// 중앙 집중형 게임 로직 레이어 - 별도 스레드에서 동작
class CCentralizedServer
{
public:
    explicit CCentralizedServer(int port, int maxClients, int mainlogicTickMs = -1);
    virtual ~CCentralizedServer();

    void Start();
    void Stop();

private:
    void GameLogicThread();

    // 네트워크 이벤트 처리 
    void DispatchClientConnected(int64_t sessionId);
    void DispatchClientDisconnected(int64_t sessionId);
    void DispatchDataReceived(int64_t sessionId, const char* data, size_t length);

    void ProcessGameLogic();

private:
    std::shared_ptr<CIOCPServer> _networkServer;
    std::thread _gameThread;
    std::atomic<bool> _running;
    int _mainlogicTickMs;
};