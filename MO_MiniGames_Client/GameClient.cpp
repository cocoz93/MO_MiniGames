
#include "GameClient.h"
#include <cstring>
#include <conio.h> // _getch();

CGameClient::CGameClient()
    : _socket(INVALID_SOCKET)
    , _connected(false)
    , _running(false)
    , _inRoom(false)
    , _currentRoomId(-1)
{
}

CGameClient::~CGameClient()
{
    Disconnect();
}

bool CGameClient::Connect(const std::string& serverIp, int port)
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
        std::cerr << "socket creation failed: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return false;
    }

    SOCKADDR_IN serverAddr;
    ZeroMemory(&serverAddr, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    inet_pton(AF_INET, serverIp.c_str(), &serverAddr.sin_addr);

    if (connect(_socket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
    {
        std::cerr << "connect failed: " << WSAGetLastError() << std::endl;
        closesocket(_socket);
        WSACleanup();
        return false;
    }

    _connected = true;
    _running = true;

    // 수신 스레드 시작
    _recvThread = std::thread(&CGameClient::RecvThread, this);

    std::cout << "==================================" << std::endl;
    std::cout << "Connected to server: " << serverIp << ":" << port << std::endl;
    std::cout << "==================================" << std::endl;

    return true;
}

void CGameClient::Disconnect()
{
    if (!_connected)
    {
        return;
    }

    _running = false;
    _connected = false;

    if (_socket != INVALID_SOCKET)
    {
        closesocket(_socket);
        _socket = INVALID_SOCKET;
    }

    if (_recvThread.joinable())
    {
        _recvThread.join();
    }

    WSACleanup();

    std::cout << "\nDisconnected from server." << std::endl;
}

void CGameClient::RecvThread()
{
    char buffer[4096];

    while (_running && _connected)
    {
        int bytesReceived = recv(_socket, buffer, sizeof(buffer), 0);

        if (bytesReceived <= 0)
        {
            if (_running)
            {
                std::cerr << "\nConnection lost." << std::endl;
            }
            _connected = false;
            _running = false;
            break;
        }

        HandleServerMessage(buffer, bytesReceived);
    }
}

bool CGameClient::SendPacket(const char* data, size_t length)
{
    if (!_connected)
    {
        return false;
    }

    int bytesSent = send(_socket, data, static_cast<int>(length), 0);
    if (bytesSent == SOCKET_ERROR)
    {
        std::cerr << "send failed: " << WSAGetLastError() << std::endl;
        return false;
    }

    return true;
}

void CGameClient::RequestRoomList()
{
    MsgHeader header;
    header.size = sizeof(MsgHeader);
    header.type = MsgType::C2S_REQUEST_ROOM_LIST;

    SendPacket(reinterpret_cast<const char*>(&header), sizeof(header));
    std::cout << "Requesting room list..." << std::endl;
}

void CGameClient::RequestCreateRoom(const std::string& title, int32_t maxPlayers)
{
    MSG_C2S_CREATE_ROOM msg;
    msg.header.size = sizeof(MSG_C2S_CREATE_ROOM);
    msg.header.type = MsgType::C2S_CREATE_ROOM;
    strncpy_s(msg.title, title.c_str(), sizeof(msg.title) - 1);
    msg.title[sizeof(msg.title) - 1] = '\0';
    msg.maxPlayers = maxPlayers;

    SendPacket(reinterpret_cast<const char*>(&msg), sizeof(msg));
    std::cout << "Requesting to create room: " << title << " (Max: " << maxPlayers << ")" << std::endl;
}

void CGameClient::RequestJoinRoom(int32_t roomId)
{
    MSG_C2S_JOIN_ROOM msg;
    msg.header.size = sizeof(MSG_C2S_JOIN_ROOM);
    msg.header.type = MsgType::C2S_JOIN_ROOM;
    msg.roomId = roomId;

    SendPacket(reinterpret_cast<const char*>(&msg), sizeof(msg));
    std::cout << "Requesting to join room: " << roomId << std::endl;
}

void CGameClient::RequestLeaveRoom()
{
    MSG_C2S_LEAVE_ROOM msg;
    msg.header.size = sizeof(MSG_C2S_LEAVE_ROOM);
    msg.header.type = MsgType::C2S_LEAVE_ROOM;

    SendPacket(reinterpret_cast<const char*>(&msg), sizeof(msg));
    std::cout << "Requesting to leave room..." << std::endl;
}

void CGameClient::HandleServerMessage(const char* data, size_t length)
{
    if (length < sizeof(MsgHeader))
    {
        std::cerr << "Invalid message size" << std::endl;
        return;
    }

    const MsgHeader* header = reinterpret_cast<const MsgHeader*>(data);

    // 메시지 크기 검증
    if (header->size > length)
    {
        std::cerr << "Message size mismatch" << std::endl;
        return;
    }

    // 메시지 타입별 처리
    switch (header->type)
    {
    case MsgType::S2C_ROOM_LIST:
        if (length >= sizeof(MSG_S2C_ROOM_LIST))
        {
            HandleRoomList(reinterpret_cast<const MSG_S2C_ROOM_LIST*>(data), length);
        }
        break;

    case MsgType::S2C_ROOM_CREATED:
        if (length >= sizeof(MSG_S2C_ROOM_CREATED))
        {
            HandleRoomCreated(reinterpret_cast<const MSG_S2C_ROOM_CREATED*>(data));
        }
        break;

    case MsgType::S2C_ROOM_JOINED:
        if (length >= sizeof(MSG_S2C_ROOM_JOINED))
        {
            HandleRoomJoined(reinterpret_cast<const MSG_S2C_ROOM_JOINED*>(data));
        }
        break;

    case MsgType::S2C_ROOM_LEFT:
        if (length >= sizeof(MSG_S2C_ROOM_LEFT))
        {
            HandleRoomLeft(reinterpret_cast<const MSG_S2C_ROOM_LEFT*>(data));
        }
        break;

    case MsgType::S2C_ERROR:
        if (length >= sizeof(MSG_S2C_ERROR))
        {
            HandleError(reinterpret_cast<const MSG_S2C_ERROR*>(data));
        }
        break;

    default:
        std::cerr << "Unknown message type: " << static_cast<int>(header->type) << std::endl;
        break;
    }
}

void CGameClient::HandleRoomList(const MSG_S2C_ROOM_LIST* msg, size_t msgSize)
{
    std::cout << "\n==================================" << std::endl;
    std::cout << "ROOM LIST (Total: " << msg->roomCount << " rooms)" << std::endl;
    std::cout << "==================================" << std::endl;

    if (msg->roomCount == 0)
    {
        std::cout << "No rooms available." << std::endl;
        std::cout << "==================================" << std::endl;
        return;
    }

    // 방 정보 파싱
    const RoomInfo* roomInfoArray = reinterpret_cast<const RoomInfo*>(
        reinterpret_cast<const char*>(msg) + sizeof(MSG_S2C_ROOM_LIST));

    std::vector<RoomInfo> rooms;
    for (int32_t i = 0; i < msg->roomCount; ++i)
    {
        rooms.push_back(roomInfoArray[i]);
    }

    DisplayRoomList(rooms);
}

void CGameClient::HandleRoomCreated(const MSG_S2C_ROOM_CREATED* msg)
{
    std::cout << "\n==================================" << std::endl;
    if (msg->success)
    {
        std::cout << "Room created successfully!" << std::endl;
        std::cout << "Room ID: " << msg->roomId << std::endl;
    }
    else
    {
        std::cout << "Failed to create room." << std::endl;
    }
    std::cout << "==================================" << std::endl;
}

void CGameClient::HandleRoomJoined(const MSG_S2C_ROOM_JOINED* msg)
{
    std::cout << "\n==================================" << std::endl;
    if (msg->success)
    {
        _inRoom = true;
        _currentRoomId = msg->roomId;
        std::cout << "Joined room successfully!" << std::endl;
        std::cout << "Room ID: " << msg->roomId << std::endl;
    }
    else
    {
        std::cout << "Failed to join room." << std::endl;
    }
    std::cout << "==================================" << std::endl;
}

void CGameClient::HandleRoomLeft(const MSG_S2C_ROOM_LEFT* msg)
{
    std::cout << "\n==================================" << std::endl;
    if (msg->success)
    {
        _inRoom = false;
        _currentRoomId = -1;
        std::cout << "Left room successfully!" << std::endl;
    }
    else
    {
        std::cout << "Failed to leave room." << std::endl;
    }
    std::cout << "==================================" << std::endl;
}

void CGameClient::HandleError(const MSG_S2C_ERROR* msg)
{
    std::cout << "\n==================================" << std::endl;
    std::cout << "[ERROR] " << msg->message << std::endl;
    std::cout << "==================================" << std::endl;
}

void CGameClient::DisplayRoomList(const std::vector<RoomInfo>& rooms)
{
    std::cout << "\n";
    std::cout << "+------+------------------------+----------+----------+" << std::endl;
    std::cout << "| ID   | TITLE                  | PLAYERS  | STATUS   |" << std::endl;
    std::cout << "+------+------------------------+----------+----------+" << std::endl;

    for (const auto& room : rooms)
    {
        std::string statusStr = (room.status == 0) ? "WAITING" : "PLAYING";

        printf("| %-4d | %-22s | %2d / %-4d | %-8s |\n",
            room.roomId,
            room.title,
            room.currentPlayers,
            room.maxPlayers,
            statusStr.c_str());
    }

    std::cout << "+------+------------------------+----------+----------+" << std::endl;
}

void CGameClient::DisplayMenu()
{
    std::cout << "\n==================================" << std::endl;
    if (_inRoom)
    {
        std::cout << "IN ROOM (ID: " << _currentRoomId << ")" << std::endl;
        std::cout << "==================================" << std::endl;
        std::cout << "[4] Leave Room" << std::endl;
        std::cout << "[0] Disconnect" << std::endl;
    }
    else
    {
        std::cout << "LOBBY MENU" << std::endl;
        std::cout << "==================================" << std::endl;
        std::cout << "[1] View Room List" << std::endl;
        std::cout << "[2] Create Room" << std::endl;
        std::cout << "[3] Join Room" << std::endl;
        std::cout << "[0] Disconnect" << std::endl;
    }
    std::cout << "==================================" << std::endl;
    std::cout << "Select: ";
}

void CGameClient::Run()
{
    while (_running && _connected)
    {
        DisplayMenu();

        int choice;
        std::cin >> choice;

        if (std::cin.fail())
        {
            std::cin.clear();
            std::cin.ignore(10000, '\n');
            std::cout << "Invalid input. Please enter a number." << std::endl;
            continue;
        }

        if (_inRoom)
        {
            // 방 안에 있을 때
            switch (choice)
            {
            case 4:
                RequestLeaveRoom();
                break;
            case 0:
                std::cout << "Disconnecting..." << std::endl;
                _running = false;
                break;
            default:
                std::cout << "Invalid choice." << std::endl;
                break;
            }
        }
        else
        {
            // 로비에 있을 때
            switch (choice)
            {
            case 1:
                RequestRoomList();
                break;

            case 2:
            {
                std::string title;
                int32_t maxPlayers;

                std::cout << "Enter room title: ";
                std::cin.ignore();
                std::getline(std::cin, title);

                std::cout << "Enter max players (2-10): ";
                std::cin >> maxPlayers;

                if (maxPlayers < 2 || maxPlayers > 10)
                {
                    std::cout << "Invalid max players. Must be between 2 and 10." << std::endl;
                    break;
                }

                RequestCreateRoom(title, maxPlayers);
                break;
            }

            case 3:
            {
                int32_t roomId;
                std::cout << "Enter room ID: ";
                std::cin >> roomId;

                RequestJoinRoom(roomId);
                break;
            }

            case 0:
                std::cout << "Disconnecting..." << std::endl;
                _running = false;
                break;

            default:
                std::cout << "Invalid choice." << std::endl;
                break;
            }
        }

        // 입력 버퍼 비우기
        std::cin.clear();
        std::cin.ignore(10000, '\n');

        // UI 갱신을 위한 짧은 대기
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    Disconnect();
}