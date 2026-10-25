#include "core/RfbClient.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>

#include "core/Strings.hpp"

namespace eduvm {

namespace {

constexpr std::int32_t kEncRaw = 0;
constexpr std::int32_t kEncCopyRect = 1;
constexpr std::int32_t kEncDesktopSize = -223;
constexpr std::int32_t kEncQemuExtKey = -258;

void put16(std::vector<std::uint8_t>& v, std::uint16_t x) {
    v.push_back(static_cast<std::uint8_t>(x >> 8));
    v.push_back(static_cast<std::uint8_t>(x));
}

void put32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    put16(v, static_cast<std::uint16_t>(x >> 16));
    put16(v, static_cast<std::uint16_t>(x));
}

}  // namespace

RfbClient::~RfbClient() { stop(); }

void RfbClient::start(int port) {
    stop();
    stopping_ = false;
    extendedKeys_ = false;
    state_ = State::Connecting;
    {
        std::lock_guard lock(errorMutex_);
        error_.clear();
    }
    thread_ = std::thread([this, port] { run(port); });
}

void RfbClient::stop() {
    stopping_ = true;
    {
        std::lock_guard lock(sendMutex_);
        socket_.shutdownBoth();
    }
    if (thread_.joinable()) thread_.join();
    state_ = State::Idle;
}

std::string RfbClient::error() const {
    std::lock_guard lock(errorMutex_);
    return error_;
}

bool RfbClient::takeFrame(std::vector<std::uint8_t>& rgba, int& width, int& height) {
    std::lock_guard lock(fbMutex_);
    if (!dirty_) return false;
    rgba = fb_;
    width = width_;
    height = height_;
    dirty_ = false;
    return true;
}

std::uint8_t RfbClient::read8() {
    std::uint8_t b;
    socket_.readExact(&b, 1);
    return b;
}

std::uint16_t RfbClient::read16() {
    std::uint8_t b[2];
    socket_.readExact(b, 2);
    return static_cast<std::uint16_t>((b[0] << 8) | b[1]);
}

std::uint32_t RfbClient::read32() {
    std::uint8_t b[4];
    socket_.readExact(b, 4);
    return (std::uint32_t(b[0]) << 24) | (std::uint32_t(b[1]) << 16) | (std::uint32_t(b[2]) << 8) | b[3];
}

void RfbClient::send(const std::vector<std::uint8_t>& msg) {
    std::lock_guard lock(sendMutex_);
    if (state_ != State::Connected) return;  // input during the handshake would corrupt it
    try {
        socket_.sendAll(std::string(msg.begin(), msg.end()));
    } catch (const Error&) {
        // The reader thread notices the broken connection and reports it.
    }
}

void RfbClient::run(int port) {
    try {
        // QEMU may still be starting; retry for a few seconds.
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (true) {
            TcpSocket s;
            try {
                s.connect("127.0.0.1", port, std::chrono::seconds(2));
                std::lock_guard lock(sendMutex_);
                socket_ = std::move(s);
                break;
            } catch (const Error&) {
                if (stopping_ || std::chrono::steady_clock::now() > deadline) throw;
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
            }
        }
        if (stopping_) return;
        handshake();
        state_ = State::Connected;
        socket_.setReadTimeout(std::chrono::milliseconds(0));  // block until the server sends something
        requestUpdate(false);

        while (!stopping_) {
            std::uint8_t type = read8();
            switch (type) {
                case 0: readFramebufferUpdate(); break;
                case 1: {  // SetColourMapEntries: unused with true-colour, skip
                    read8();
                    read16();
                    std::uint16_t n = read16();
                    scratch_.resize(n * 6u);
                    socket_.readExact(scratch_.data(), scratch_.size());
                    break;
                }
                case 2: break;  // Bell
                case 3: {       // ServerCutText: clipboard sync is not supported yet
                    std::uint8_t pad[3];
                    socket_.readExact(pad, 3);
                    std::uint32_t len = read32();
                    scratch_.resize(len);
                    if (len) socket_.readExact(scratch_.data(), len);
                    break;
                }
                default: throw Error("Unsupported VNC message " + std::to_string(type));
            }
        }
    } catch (const Error& e) {
        if (!stopping_) {
            std::lock_guard lock(errorMutex_);
            error_ = e.what();
        }
    }
    state_ = State::Disconnected;
    std::lock_guard lock(sendMutex_);
    socket_.close();
}

void RfbClient::handshake() {
    char version[12];
    socket_.readExact(version, sizeof(version));
    if (std::memcmp(version, "RFB ", 4) != 0) throw Error("Not a VNC server");
    socket_.sendAll("RFB 003.008\n");

    std::uint8_t count = read8();
    if (count == 0) {
        std::string reason(read32(), '\0');
        socket_.readExact(reason.data(), reason.size());
        throw Error("VNC server refused the connection: " + reason);
    }
    std::vector<std::uint8_t> types(count);
    socket_.readExact(types.data(), count);
    if (std::find(types.begin(), types.end(), 1) == types.end()) throw Error("VNC server requires a password");
    socket_.sendAll(std::string(1, '\x01'));
    if (read32() != 0) throw Error("VNC security handshake failed");

    socket_.sendAll(std::string(1, '\x01'));  // ClientInit: shared session
    std::uint16_t w = read16();
    std::uint16_t h = read16();
    std::uint8_t serverFormat[16];
    socket_.readExact(serverFormat, sizeof(serverFormat));
    std::string name(read32(), '\0');
    socket_.readExact(name.data(), name.size());
    {
        std::lock_guard lock(fbMutex_);
        width_ = w;
        height_ = h;
        fb_.assign(static_cast<size_t>(w) * h * 4, 0);
        for (size_t i = 3; i < fb_.size(); i += 4) fb_[i] = 255;
        dirty_ = true;
    }

    // 32bpp little-endian, R at bit 0: bytes arrive as R,G,B,X.
    std::vector<std::uint8_t> fmt = {0, 0, 0, 0, 32, 24, 0, 1};
    put16(fmt, 255);
    put16(fmt, 255);
    put16(fmt, 255);
    fmt.insert(fmt.end(), {0, 8, 16, 0, 0, 0});
    socket_.sendAll(std::string(fmt.begin(), fmt.end()));

    std::vector<std::uint8_t> enc = {2, 0};
    const std::int32_t encodings[] = {kEncCopyRect, kEncRaw, kEncDesktopSize, kEncQemuExtKey};
    put16(enc, static_cast<std::uint16_t>(std::size(encodings)));
    for (std::int32_t e : encodings) put32(enc, static_cast<std::uint32_t>(e));
    socket_.sendAll(std::string(enc.begin(), enc.end()));
}

void RfbClient::requestUpdate(bool incremental) {
    int w, h;
    {
        std::lock_guard lock(fbMutex_);
        w = width_;
        h = height_;
    }
    std::vector<std::uint8_t> msg = {3, static_cast<std::uint8_t>(incremental ? 1 : 0)};
    put16(msg, 0);
    put16(msg, 0);
    put16(msg, static_cast<std::uint16_t>(w));
    put16(msg, static_cast<std::uint16_t>(h));
    send(msg);
}

void RfbClient::readFramebufferUpdate() {
    read8();  // padding
    std::uint16_t rects = read16();
    bool resized = false;

    for (std::uint16_t i = 0; i < rects; ++i) {
        int x = read16(), y = read16(), w = read16(), h = read16();
        auto encoding = static_cast<std::int32_t>(read32());

        if (encoding == kEncRaw) {
            scratch_.resize(static_cast<size_t>(w) * h * 4);
            if (!scratch_.empty()) socket_.readExact(scratch_.data(), scratch_.size());
            std::lock_guard lock(fbMutex_);
            for (int row = 0; row < h && y + row < height_; ++row) {
                const std::uint8_t* src = scratch_.data() + static_cast<size_t>(row) * w * 4;
                std::uint8_t* dst = fb_.data() + (static_cast<size_t>(y + row) * width_ + x) * 4;
                int cols = std::min(w, width_ - x);
                for (int c = 0; c < cols; ++c) {
                    dst[c * 4 + 0] = src[c * 4 + 0];
                    dst[c * 4 + 1] = src[c * 4 + 1];
                    dst[c * 4 + 2] = src[c * 4 + 2];
                    dst[c * 4 + 3] = 255;
                }
            }
            dirty_ = true;
        } else if (encoding == kEncCopyRect) {
            int sx = read16(), sy = read16();
            std::lock_guard lock(fbMutex_);
            if (sx + w > width_ || sy + h > height_ || x + w > width_ || y + h > height_) continue;
            std::vector<std::uint8_t> tmp(static_cast<size_t>(w) * h * 4);  // handles overlapping regions
            for (int row = 0; row < h; ++row)
                std::memcpy(&tmp[static_cast<size_t>(row) * w * 4], &fb_[(static_cast<size_t>(sy + row) * width_ + sx) * 4], w * 4u);
            for (int row = 0; row < h; ++row)
                std::memcpy(&fb_[(static_cast<size_t>(y + row) * width_ + x) * 4], &tmp[static_cast<size_t>(row) * w * 4], w * 4u);
            dirty_ = true;
        } else if (encoding == kEncDesktopSize) {
            std::lock_guard lock(fbMutex_);
            width_ = w;
            height_ = h;
            fb_.assign(static_cast<size_t>(w) * h * 4, 0);
            for (size_t p = 3; p < fb_.size(); p += 4) fb_[p] = 255;
            dirty_ = true;
            resized = true;
        } else if (encoding == kEncQemuExtKey) {
            extendedKeys_ = true;
        } else {
            throw Error("VNC server used unsupported encoding " + std::to_string(encoding));
        }
    }
    requestUpdate(!resized);
}

void RfbClient::sendPointer(int x, int y, std::uint8_t buttons) {
    std::vector<std::uint8_t> msg = {5, buttons};
    put16(msg, static_cast<std::uint16_t>(std::max(0, x)));
    put16(msg, static_cast<std::uint16_t>(std::max(0, y)));
    send(msg);
}

void RfbClient::sendKey(std::uint32_t keysym, std::uint32_t xtKeycode, bool down) {
    std::vector<std::uint8_t> msg;
    if (extendedKeys_ && xtKeycode != 0) {
        msg = {255, 0};
        put16(msg, down ? 1 : 0);
        put32(msg, keysym);
        put32(msg, xtKeycode);
    } else if (keysym != 0) {
        msg = {4, static_cast<std::uint8_t>(down ? 1 : 0), 0, 0};
        put32(msg, keysym);
    } else {
        return;
    }
    send(msg);
}

void RfbClient::sendCtrlAltDel() {
    const std::uint32_t ctrl = 0xffe3, alt = 0xffe9, del = 0xffff;
    sendKey(ctrl, 0x1d, true);
    sendKey(alt, 0x38, true);
    sendKey(del, 0xd3, true);
    sendKey(del, 0xd3, false);
    sendKey(alt, 0x38, false);
    sendKey(ctrl, 0x1d, false);
}

}  // namespace eduvm
