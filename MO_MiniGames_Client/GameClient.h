#pragma once

#include "Protocol.h"
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <iostream>

#pragma comment(lib, "ws2_32.lib")

class CGameClient
{
public:
    CGameClient();
    ~CGameClient();

    bool Connect(const std::string& serverIp, int port);
    void Disconnect();
    bool IsConnected() const { return _connected; }

    // 서버로 요청 전송
    void RequestRoomList();
    void RequestCreateRoom(const std::string& title, int32_t maxPlayers);
    void RequestJoinRoom(int32_t roomId);
    void RequestLeaveRoom();

    // 메인 루프 (콘솔 입력 처리)
    void Run();

private:
    // 수신 스레드
    void RecvThread();

    // 패킷 전송
    bool SendPacket(const char* data, size_t length);

    // 서버 응답 처리
    void HandleServerMessage(const char* data, size_t length);
    void HandleRoomList(const MSG_S2C_ROOM_LIST* msg, size_t msgSize);
    void HandleRoomCreated(const MSG_S2C_ROOM_CREATED* msg);
    void HandleRoomJoined(const MSG_S2C_ROOM_JOINED* msg);
    void HandleRoomLeft(const MSG_S2C_ROOM_LEFT* msg);
    void HandleError(const MSG_S2C_ERROR* msg);

    // UI 헬퍼
    void DisplayMenu();
    void DisplayRoomList(const std::vector<RoomInfo>& rooms);

private:
    SOCKET _socket;
    std::atomic<bool> _connected;
    std::atomic<bool> _running;
    std::thread _recvThread;

    // 클라이언트 상태
    bool _inRoom;
    int32_t _currentRoomId;
};