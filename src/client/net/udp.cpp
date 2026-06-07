#include "network.h"

#include <cstring>
#include <string>
#include <optional>
#include <chrono>
#include <thread>
#include <sstream>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
using sock_t = SOCKET;
constexpr sock_t INVALID = INVALID_SOCKET;
static void sock_close(sock_t s) { closesocket(s); }
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <fcntl.h>
using sock_t = int;
constexpr sock_t INVALID = -1;
static void sock_close(sock_t s) { close(s); }
#endif

namespace mag {

namespace {

// Parse "MAG_SERVER <ip> <port>" into a ServerInfo.
std::optional<ServerInfo> parse_server_msg(const char* buf, int len) {
    std::string msg(buf, len);
    if (msg.rfind("MAG_SERVER ", 0) != 0) return std::nullopt;
    std::istringstream ss(msg.substr(11));
    ServerInfo info;
    int port;
    if (ss >> info.ip >> port) {
        info.port = port;
        return info;
    }
    return std::nullopt;
}

#ifdef _WIN32
struct WsaGuard {
    WsaGuard() { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); }
    ~WsaGuard() { WSACleanup(); }
};
static WsaGuard _wsa_guard;
#endif

} // namespace

std::optional<ServerInfo> discover_server(int udp_port, int timeout_sec) {
    sock_t sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID) return std::nullopt;

    // Allow broadcast
    int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&yes), sizeof(yes));
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));

    // Bind to receive broadcasts
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(static_cast<uint16_t>(udp_port));
    local.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock, reinterpret_cast<sockaddr*>(&local), sizeof(local)) < 0) {
        sock_close(sock);
        return std::nullopt;
    }

    // Set 1s recv timeout so we can retry
#ifdef _WIN32
    DWORD tv_ms = 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv_ms), sizeof(tv_ms));
#else
    timeval tv{1, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    // Broadcast address
    sockaddr_in bcast{};
    bcast.sin_family = AF_INET;
    bcast.sin_port = htons(static_cast<uint16_t>(udp_port));
    bcast.sin_addr.s_addr = INADDR_BROADCAST;

    const char* who = "MAG_WHO";
    auto start = std::chrono::steady_clock::now();

    while (true) {
        // Send MAG_WHO broadcast
        sendto(sock, who, static_cast<int>(strlen(who)), 0,
               reinterpret_cast<const sockaddr*>(&bcast), sizeof(bcast));

        // Listen for 3 seconds worth of 1s receive windows
        for (int attempt = 0; attempt < 3; ++attempt) {
            char buf[256]{};
            sockaddr_in from{};
            socklen_t fromlen = sizeof(from);
            int n = recvfrom(sock, buf, sizeof(buf) - 1, 0,
                             reinterpret_cast<sockaddr*>(&from), &fromlen);
            if (n > 0) {
                auto info = parse_server_msg(buf, n);
                if (info) {
                    sock_close(sock);
                    return info;
                }
            }
            if (timeout_sec > 0) {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - start).count();
                if (elapsed >= timeout_sec) {
                    sock_close(sock);
                    return std::nullopt;
                }
            }
        }
    }
}

} // namespace mag
