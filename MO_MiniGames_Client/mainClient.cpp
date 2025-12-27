//

#include "GameClient.h"
#include <iostream>
#include <string>

int main()
{
    std::cout << "==================================" << std::endl;
    std::cout << "     Game Client v1.0" << std::endl;
    std::cout << "==================================" << std::endl;

    std::string serverIp;
    int port;

    std::cout << "Enter server IP (default: 127.0.0.1): ";
    std::getline(std::cin, serverIp);
    if (serverIp.empty())
    {
        serverIp = "127.0.0.1";
    }

    std::cout << "Enter server port (default: 9000): ";
    std::string portStr;
    std::getline(std::cin, portStr);
    if (portStr.empty())
    {
        port = 9000;
    }
    else
    {
        port = std::stoi(portStr);
    }

    CGameClient client;

    if (!client.Connect(serverIp, port))
    {
        std::cerr << "Failed to connect to server." << std::endl;
        return 1;
    }

    // 메인 루프 실행
    client.Run();

    std::cout << "Client terminated." << std::endl;
    return 0;
}