#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace eduvm {

// Minimal blocking TCP client for localhost control channels (QMP, guest
// agent). Both protocols are newline-delimited JSON, hence readLine().
class TcpSocket {
public:
    TcpSocket() = default;
    ~TcpSocket();
    TcpSocket(const TcpSocket&) = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;
    TcpSocket(TcpSocket&& other) noexcept;
    TcpSocket& operator=(TcpSocket&& other) noexcept;

    void connect(const std::string& host, int port, std::chrono::milliseconds timeout);
    void setReadTimeout(std::chrono::milliseconds timeout);
    void sendAll(const std::string& data);
    // Returns one line without the trailing newline. Throws on timeout/EOF.
    std::string readLine();
    void readExact(void* dest, size_t n);
    // Unblocks a thread sitting in a read on this socket (used to stop the VNC client).
    void shutdownBoth();
    void close();
    bool isOpen() const { return fd_ != kInvalid; }

private:
#ifdef _WIN32
    using Handle = std::uintptr_t;
#else
    using Handle = int;
#endif
    static constexpr Handle kInvalid = static_cast<Handle>(-1);
    Handle fd_ = kInvalid;
    std::string buffer_;
};

// Asks the OS for a currently unused localhost TCP port.
int findFreePort();
// Like findFreePort, but within [lo, hi]; needed for VNC (ports start at 5900).
int findFreePortInRange(int lo, int hi);

}  // namespace eduvm
