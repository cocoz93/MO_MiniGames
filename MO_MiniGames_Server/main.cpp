//
#include <iostream>
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include <condition_variable>
#include <mutex>

#include "IOCPServer.h"
#include "GameServer.h"

std::atomic<bool> running{true};
std::mutex mtx;
std::condition_variable cv;

// 프로세스 전체 종료 컨트롤러이므로 메인문에 빼둔다
void SignalProcessShutdown()
{
    running = false;
    cv.notify_one();
}

int main()
{
    constexpr int PORT = 9000;
    constexpr int MAX_CLIENTS = 1000;

    std::cout << "=== IOCP Mini Game Server ===" << std::endl;
    std::cout << "Port: " << PORT << std::endl;
    std::cout << "Max Clients: " << MAX_CLIENTS << std::endl;

    // 네트워크 레이어 생성
    auto networkServer = std::make_shared<CIOCPServer>(PORT, MAX_CLIENTS);

    if (!networkServer->Initialize())
    {
        std::cerr << "Failed to initialize network server" << std::endl;
        return 1;
    }

    std::cout << "Network server initialized successfully" << std::endl;

    // 게임 로직 레이어 생성
    auto gameServer = std::make_unique<CGameServer>(networkServer);

    // 서버 시작
    networkServer->Start();
    gameServer->Start();

    // main 스레드는 condition_variable로 대기
    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&] { return !running; });
    }

    // 서버 종료
    gameServer->Stop();
    networkServer->Disconnect();

    std::cout << "Server shutdown complete" << std::endl;
    return 0;
}