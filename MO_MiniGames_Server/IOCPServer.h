#pragma once

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <queue>
#include <functional>

#pragma comment(lib, "ws2_32.lib")

constexpr int BUFFER_SIZE = 4096;

enum class IOOperation
{
    RECV,
    SEND,
    ACCEPT
};

// 서버 아키텍처 타입
enum class ServerArchitectureType
{
    Centralized,    // 중앙 집중형 - 별도 스레드에서 이벤트 처리
    Partitioned,    // 분산형 - 여러 스레드/큐로 분리 처리
    UnifiedStrand   // 통합 스트랜드 - IOCP 워커가 게임 로직까지 직접 처리
};

struct OverlappedEx
{
    OVERLAPPED overlapped;
    WSABUF wsaBuf;
    char buffer[BUFFER_SIZE];
    IOOperation operation;

    OverlappedEx()
        : operation(IOOperation::RECV)
    {
        ZeroMemory(&overlapped, sizeof(OVERLAPPED));
        ZeroMemory(buffer, BUFFER_SIZE);
        wsaBuf.buf = buffer;
        wsaBuf.len = BUFFER_SIZE;
    }
};

class CSession
{
public:
    explicit CSession(SOCKET socket, int64_t sessionId);
    virtual ~CSession();

    SOCKET GetSocket() const;
    int64_t GetSessionId() const;
    bool IsConnected() const;
    void SetConnected(bool connected);

    OverlappedEx* GetRecvOverlapped();
    OverlappedEx* GetSendOverlapped();

    void Close();

private:
    SOCKET _socket;
    int64_t _sessionId;
    std::atomic<bool> _connected;
    OverlappedEx _recvOverlapped;
    OverlappedEx _sendOverlapped;
};

// 게임 로직 레이어로 전달할 네트워크 이벤트
struct NetworkEvent
{
    enum class Type
    {
        CONNECTED,
        DISCONNECTED,
        RECEIVED
    };

    Type type;
    int64_t sessionId;
    std::vector<char> data;

    NetworkEvent(Type t, int64_t id)
        : type(t), sessionId(id)
    {
    }

    NetworkEvent(Type t, int64_t id, const char* buffer, size_t length)
        : type(t), sessionId(id), data(buffer, buffer + length)
    {
    }
};

// 게임 로직에서 네트워크 레이어로 보낼 명령
struct NetworkCommand
{
    enum class Type
    {
        SEND_PACKET,
        DISCONNECT_SESSION,
        BROADCAST_PACKET
    };

    Type type;
    int64_t sessionId;
    std::vector<char> data;

    NetworkCommand(Type t, int64_t id)
        : type(t), sessionId(id)
    {
    }

    NetworkCommand(Type t, int64_t id, const char* buffer, size_t length)
        : type(t), sessionId(id), data(buffer, buffer + length)
    {
    }

    // Broadcast용
    NetworkCommand(Type t, const char* buffer, size_t length)
        : type(t), sessionId(-1), data(buffer, buffer + length)
    {
    }
};

// 스레드 안전한 큐
template<typename T>
class ThreadSafeQueue
{
public:
    void Push(T&& item)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _queue.push(std::move(item));
    }

    bool TryPop(T& item)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_queue.empty())
        {
            return false;
        }
        item = std::move(_queue.front());
        _queue.pop();
        return true;
    }

    bool IsEmpty() const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _queue.empty();
    }

private:
    std::queue<T> _queue;
    mutable std::mutex _mutex;
};

// 네트워크 I/O 처리 레이어
class CIOCPServer
{
public:
    explicit CIOCPServer(int port, int maxClients, ServerArchitectureType type = ServerArchitectureType::Centralized);
    virtual ~CIOCPServer();

    bool Initialize();
    void Start();
    void Disconnect();

    // 게임 로직 레이어가 사용할 인터페이스
    void RequestSendPacket(int64_t sessionId, const char* data, int length);
    void RequestDisconnectSession(int64_t sessionId);
    void RequestBroadcastPacket(const char* data, int length);

    // 게임 로직 레이어로 전달할 이벤트 가져오기 (QUEUE_BASED 모드용)
    bool PopNetworkEvent(NetworkEvent& event);

    // 처리 방식 타입 가져오기
    ServerArchitectureType GetArchitectureType() const;

protected:
    // 다이렉트 모드용(UnifiedStrand) - 하위 클래스에서 오버라이드
    virtual void OnClientConnected(int64_t sessionId);
    virtual void OnClientDisconnected(int64_t sessionId);
    virtual void OnDataReceived(int64_t sessionId, const char* data, size_t length);

private:
    // 게임 로직으로 이벤트 전달 (QUEUE_BASED 모드용)
    void PushNetworkEvent(NetworkEvent&& event);

    void WorkerThread();
    void AcceptThread();
    void CommandProcessThread();

    bool CreateListenSocket();
    bool BindIOCP(SOCKET socket, ULONG_PTR completionKey);

    void ProcessAccept(SOCKET clientSocket);
    void ProcessRecv(CSession* session, DWORD bytesTransferred);
    void ProcessSend(CSession* session, DWORD bytesTransferred);

    std::shared_ptr<CSession> GetSession(int64_t sessionId);
    void AddSession(std::shared_ptr<CSession> session);
    void RemoveSession(int64_t sessionId);

private:
    int _port;
    int _maxClients;
    ServerArchitectureType _architectureType;
    std::atomic<bool> _running;
    std::atomic<int64_t> _sessionIdCounter;

    SOCKET _listenSocket;
    HANDLE _iocpHandle;

    std::vector<std::thread> _workerThreads;
    std::thread _acceptThread;
    std::thread _commandThread;

    std::unordered_map<int64_t, std::shared_ptr<CSession>> _sessions;
    std::mutex _sessionMutex;

    // 레이어 간 통신 큐 (QUEUE_BASED 모드용)
    ThreadSafeQueue<NetworkEvent> _eventQueue;    // 네트워크 -> 게임 로직
    ThreadSafeQueue<NetworkCommand> _commandQueue; // 게임 로직 -> 네트워크
};