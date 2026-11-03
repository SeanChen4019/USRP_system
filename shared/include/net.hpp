#pragma once
#include "core.hpp"
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
namespace sl {
    struct Network {
        Network() {
#ifdef _WIN32
            WSADATA d;
            if (WSAStartup(MAKEWORD(2, 2), &d))
                throw std::runtime_error("WSAStartup failed");
#endif
        }
        ~Network() {
#ifdef _WIN32
            WSACleanup();
#endif
        }
    };
    inline sockaddr_in address(const std::string &value) {
        auto colon = value.rfind(':');
        if (colon == std::string::npos)
            throw std::runtime_error("expected IPv4:port");
        sockaddr_in a{};
        a.sin_family = AF_INET;
        int port = std::stoi(value.substr(colon + 1));
        if (port < 1 || port > 65535 ||
            inet_pton(AF_INET, value.substr(0, colon).c_str(), &a.sin_addr) != 1)
            throw std::runtime_error("invalid IPv4:port");
        a.sin_port = htons(uint16_t(port));
        return a;
    }
    class Socket {
#ifdef _WIN32
        SOCKET fd = INVALID_SOCKET;
#else
        int fd = -1;
#endif
      public:
        Socket() {
            fd = ::socket(AF_INET, SOCK_DGRAM, 0);
            if (fd == decltype(fd)(-1))
                throw std::runtime_error("socket failed");
            int n = 4 * 1024 * 1024;
            setsockopt(fd, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char *>(&n), sizeof n);
            setsockopt(fd, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char *>(&n), sizeof n);
        }
        Socket(const Socket &) = delete;
        Socket &operator=(const Socket &) = delete;
        ~Socket() {
#ifdef _WIN32
            closesocket(fd);
#else
            close(fd);
#endif
        }
        void bind(const std::string &s) {
            auto a = address(s);
            if (::bind(fd, reinterpret_cast<sockaddr *>(&a), sizeof a))
                throw std::runtime_error("bind failed: " + s);
        }
        void send(const Bytes &b, const sockaddr_in &a) {
            int n = ::sendto(fd, reinterpret_cast<const char *>(b.data()), int(b.size()), 0,
                             reinterpret_cast<const sockaddr *>(&a), sizeof a);
            if (n != int(b.size()))
                throw std::runtime_error("UDP send failed");
        }
        int receive(uint8_t *b, size_t cap, int timeout_ms) {
            fd_set f;
            FD_ZERO(&f);
            FD_SET(fd, &f);
            timeval t{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
            int r = select(int(fd + 1), &f, nullptr, nullptr, &t);
            if (r < 0)
                throw std::runtime_error("select failed");
            if (!r)
                return 0;
            int n = recvfrom(fd, reinterpret_cast<char *>(b), int(cap), 0, nullptr, nullptr);
            if (n < 0)
                throw std::runtime_error("UDP receive failed (oversized datagram or socket error)");
            return n;
        }
    };
} // namespace sl
