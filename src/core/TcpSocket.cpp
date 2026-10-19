#include "core/TcpSocket.hpp"

#include "core/Strings.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#endif

namespace eduvm {

namespace {

#ifdef _WIN32
struct WinsockInit {
    WinsockInit() {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
    }
    ~WinsockInit() { WSACleanup(); }
};
void ensureNetworking() { static WinsockInit init; }
int lastSocketError() { return WSAGetLastError(); }
void closeSocket(std::uintptr_t s) { closesocket(static_cast<SOCKET>(s)); }
#else
void ensureNetworking() {}
int lastSocketError() { return errno; }
void closeSocket(int s) { ::close(s); }
#endif

}  // namespace

TcpSocket::~TcpSocket() { close(); }

TcpSocket::TcpSocket(TcpSocket&& other) noexcept : fd_(other.fd_), buffer_(std::move(other.buffer_)) {
    other.fd_ = kInvalid;
}

TcpSocket& TcpSocket::operator=(TcpSocket&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        buffer_ = std::move(other.buffer_);
        other.fd_ = kInvalid;
    }
    return *this;
}

void TcpSocket::close() {
    if (fd_ != kInvalid) {
        closeSocket(fd_);
        fd_ = kInvalid;
    }
    buffer_.clear();
}

void TcpSocket::connect(const std::string& host, int port, std::chrono::milliseconds timeout) {
    ensureNetworking();
    close();

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<unsigned short>(port));
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) throw Error("Invalid IPv4 address: " + host);

    auto s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#ifdef _WIN32
    if (s == INVALID_SOCKET) throw Error("socket() failed: " + std::to_string(lastSocketError()));
    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);
#else
    if (s < 0) throw Error("socket() failed: " + std::to_string(lastSocketError()));
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
    fd_ = static_cast<Handle>(s);

    // Non-blocking connect + select gives us a connect timeout.
    ::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    fd_set writeSet, errorSet;
    FD_ZERO(&writeSet);
    FD_ZERO(&errorSet);
    FD_SET(s, &writeSet);
    FD_SET(s, &errorSet);
    timeval tv{static_cast<long>(timeout.count() / 1000), static_cast<long>((timeout.count() % 1000) * 1000)};
    int ready = select(static_cast<int>(s) + 1, nullptr, &writeSet, &errorSet, &tv);

    int soError = 0;
    socklen_t len = sizeof(soError);
    getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soError), &len);
    if (ready <= 0 || FD_ISSET(s, &errorSet) || soError != 0) {
        close();
        throw Error("Cannot connect to " + host + ":" + std::to_string(port));
    }

#ifdef _WIN32
    nonBlocking = 0;
    ioctlsocket(s, FIONBIO, &nonBlocking);
#else
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) & ~O_NONBLOCK);
#endif
    setReadTimeout(std::chrono::seconds(10));
}

void TcpSocket::setReadTimeout(std::chrono::milliseconds timeout) {
    if (fd_ == kInvalid) return;
#ifdef _WIN32
    DWORD ms = static_cast<DWORD>(timeout.count());
    setsockopt(static_cast<SOCKET>(fd_), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&ms), sizeof(ms));
#else
    timeval tv{static_cast<time_t>(timeout.count() / 1000), static_cast<suseconds_t>((timeout.count() % 1000) * 1000)};
    setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
}

void TcpSocket::sendAll(const std::string& data) {
    if (fd_ == kInvalid) throw Error("sendAll on closed socket");
    size_t sent = 0;
    while (sent < data.size()) {
#ifdef _WIN32
        int n = ::send(static_cast<SOCKET>(fd_), data.data() + sent, static_cast<int>(data.size() - sent), 0);
#else
        ssize_t n = ::send(fd_, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
#endif
        if (n <= 0) throw Error("Socket send failed: " + std::to_string(lastSocketError()));
        sent += static_cast<size_t>(n);
    }
}

std::string TcpSocket::readLine() {
    if (fd_ == kInvalid) throw Error("readLine on closed socket");
    while (true) {
        size_t nl = buffer_.find('\n');
        if (nl != std::string::npos) {
            std::string line = buffer_.substr(0, nl);
            buffer_.erase(0, nl + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return line;
        }
        char chunk[4096];
#ifdef _WIN32
        int n = ::recv(static_cast<SOCKET>(fd_), chunk, sizeof(chunk), 0);
#else
        ssize_t n = ::recv(fd_, chunk, sizeof(chunk), 0);
#endif
        if (n == 0) throw Error("Connection closed by peer");
        if (n < 0) throw Error("Timed out waiting for a response");
        buffer_.append(chunk, static_cast<size_t>(n));
    }
}

int findFreePort() {
    ensureNetworking();
    auto s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    int port = 0;
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
        socklen_t len = sizeof(addr);
        if (getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len) == 0) port = ntohs(addr.sin_port);
    }
    closeSocket(s);
    if (port == 0) throw Error("Could not find a free TCP port");
    return port;
}

}  // namespace eduvm
