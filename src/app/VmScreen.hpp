#pragma once

#include <imgui.h>

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "core/RfbClient.hpp"

namespace eduvm::app {

// Shows a VM's display inside the EduVM window. Click the screen to send
// keyboard input to the VM; press Ctrl+Alt (or click outside) to release it,
// like VMware. Ctrl+Alt+Enter toggles full screen.
class VmScreen {
public:
    explicit VmScreen(std::string vmName);
    ~VmScreen();
    VmScreen(const VmScreen&) = delete;
    VmScreen& operator=(const VmScreen&) = delete;

    void connect(int vncPort);
    int port() const { return port_; }
    RfbClient::State state() const { return client_.state(); }
    std::string error() const { return client_.error(); }
    bool hasKeyboard() const { return focused_ == this; }
    void sendCtrlAltDel() { client_.sendCtrlAltDel(); }
    void focusKeyboard() { focused_ = this; }
    int guestWidth() const { return texW_; }
    int guestHeight() const { return texH_; }

    void draw(const ImVec2& size);

    // Wired to GLFW's key callback in main.cpp.
    static void handleKey(int key, int scancode, int action, int mods);
    static bool anyHasKeyboard() { return focused_ != nullptr; }
    static void setFullscreenShortcut(std::function<void()> fn) { fullscreenShortcut_ = std::move(fn); }

private:
    void upload();
    void releaseKeyboard();

    std::string name_;
    RfbClient client_;
    int port_ = 0;
    unsigned int texture_ = 0;
    int texW_ = 0, texH_ = 0;
    std::vector<std::uint8_t> frame_;
    int lastX_ = -1, lastY_ = -1;
    std::uint8_t lastButtons_ = 0;
    std::set<std::pair<std::uint32_t, std::uint32_t>> heldKeys_;  // (keysym, xt) to release on blur

    static VmScreen* focused_;
    static bool ctrlDown_, altDown_;
    static std::function<void()> fullscreenShortcut_;
};

}  // namespace eduvm::app
