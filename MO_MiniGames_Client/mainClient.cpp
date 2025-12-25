//
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
#include <string>

#pragma comment(lib, "ws2_32.lib")

constexpr int BUFFER_SIZE = 4096;

class CClient
{
public:
    CClient(const std::string& serverIp, int port)
        : _serverIp(serverIp), _port(port), _socket(INVALID_SOCKET), _running(false)
    {
    }

    ~CClient()
    {
        Disconnect();
    }

    bool Connect()
    {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        {
            std::cerr << "WSAStartup failed" << std::endl;
            return false;
        }

        _socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (_socket == INVALID_SOCKET)
        {
            std::cerr << "socket failed: " << WSAGetLastError() << std::endl;
            WSACleanup();
            return false;
        }

        sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        inet_pton(AF_INET, _serverIp.c_str(), &serverAddr.sin_addr);
        serverAddr.sin_port = htons(_port);

        if (connect(_socket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
        {
            std::cerr << "connect failed: " << WSAGetLastError() << std::endl;
            closesocket(_socket);
            WSACleanup();
            return false;
        }

        _running = true;
        _recvThread = std::thread(&CClient::RecvThread, this);

        std::cout << "Connected to server: " << _serverIp << ":" << _port << std::endl;
        return true;
    }

    void Disconnect()
    {
        if (!_running)
            return;

        _running = false;

        if (_socket != INVALID_SOCKET)
        {
            shutdown(_socket, SD_BOTH);
            closesocket(_socket);
            _socket = INVALID_SOCKET;
        }

        if (_recvThread.joinable())
            _recvThread.join();

        WSACleanup();
        std::cout << "Disconnected from server." << std::endl;
    }

    void Send(const char* data, int length)
    {
        if (_socket == INVALID_SOCKET)
            return;

        int sent = send(_socket, data, length, 0);
        if (sent == SOCKET_ERROR)
        {
            std::cerr << "send failed: " << WSAGetLastError() << std::endl;
        }
    }

    void Send(const std::string& msg)
    {
        Send(msg.c_str(), static_cast<int>(msg.size()));
    }

private:
    void RecvThread()
    {
        char buffer[BUFFER_SIZE];
        while (_running)
        {
            int ret = recv(_socket, buffer, BUFFER_SIZE, 0);
            if (ret > 0)
            {
                std::string msg(buffer, buffer + ret);
                std::cout << "[Server] " << msg << std::endl;
            }
            else if (ret == 0)
            {
                std::cout << "Server closed connection." << std::endl;
                break;
            }
            else
            {
                std::cerr << "recv failed: " << WSAGetLastError() << std::endl;
                break;
            }
        }
        _running = false;
    }

private:
    std::string _serverIp;
    int _port;
    SOCKET _socket;
    std::atomic<bool> _running;
    std::thread _recvThread;
};

int main()
{
    std::string serverIp = "127.0.0.1";
    int port = 9000;

    CClient client(serverIp, port);

    if (!client.Connect())
        return 1;

    std::cout << "Type messages to send to server. Type 'exit' to quit." << std::endl;

    std::string line;
    while (std::getline(std::cin, line))
    {
        if (line == "exit")
            break;
        client.Send(line);
    }

    client.Disconnect();
    return 0;
}