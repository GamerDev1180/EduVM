#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/TcpSocket.hpp"

namespace eduvm {

// Minimal RFB (VNC) client for QEMU's local display: no authentication,
// Raw + CopyRect encodings, desktop resize, and QEMU's extended key event
// so keys are sent as scancodes (the guest's keyboard layout just works).
class RfbClient {
public:
    enum class State { Idle, Connecting, Connected, Disconnected };

    RfbClient() = default;
    ~RfbClient();
    RfbClient(const RfbClient&) = delete;
    RfbClient& operator=(const RfbClient&) = delete;

    void start(int port);
    void stop();

    State state() const { return state_; }
    std::string error() const;

    // Copies the framebuffer (RGBA, alpha forced to 255) if it changed since
    // the last call. Returns false when there is nothing new.
    bool takeFrame(std::vector<std::uint8_t>& rgba, int& width, int& height);

    // buttons: bit0 left, bit1 middle, bit2 right, bit3 wheel up, bit4 wheel down
    void sendPointer(int x, int y, std::uint8_t buttons);
    // xtKeycode: XT scancode, extended keys as 0x80|code (0 = unknown).
    void sendKey(std::uint32_t keysym, std::uint32_t xtKeycode, bool down);
    void sendCtrlAltDel();

private:
    void run(int port);
    void handshake();
    void readFramebufferUpdate();
    void requestUpdate(bool incremental);
    void send(const std::vector<std::uint8_t>& msg);

    std::uint8_t read8();
    std::uint16_t read16();
    std::uint32_t read32();

    TcpSocket socket_;
    std::thread thread_;
    std::atomic<State> state_{State::Idle};
    std::atomic<bool> stopping_{false};
    std::atomic<bool> extendedKeys_{false};

    mutable std::mutex errorMutex_;
    std::string error_;

    std::mutex sendMutex_;  // also guards socket_ replacement/shutdown

    std::mutex fbMutex_;
    std::vector<std::uint8_t> fb_;
    int width_ = 0;
    int height_ = 0;
    bool dirty_ = false;

    std::vector<std::uint8_t> scratch_;  // receive buffer, reader thread only
};

}  // namespace eduvm
