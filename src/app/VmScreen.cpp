#include "app/VmScreen.hpp"

#include <GLFW/glfw3.h>  // also brings in the system OpenGL 1.1 header

#include <algorithm>
#include <cstdint>

#include "app/Keymap.hpp"
#include "app/Theme.hpp"

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace eduvm::app {

VmScreen* VmScreen::focused_ = nullptr;
bool VmScreen::ctrlDown_ = false;
bool VmScreen::altDown_ = false;
std::function<void()> VmScreen::fullscreenShortcut_;

VmScreen::VmScreen(std::string vmName) : name_(std::move(vmName)) {}

VmScreen::~VmScreen() {
    if (focused_ == this) releaseKeyboard();
    client_.stop();
    if (texture_) {
        GLuint t = texture_;
        glDeleteTextures(1, &t);
    }
}

void VmScreen::connect(int vncPort) {
    port_ = vncPort;
    client_.start(vncPort);
}

void VmScreen::upload() {
    int w = 0, h = 0;
    if (!client_.takeFrame(frame_, w, h) || w <= 0 || h <= 0) return;
    if (!texture_) {
        GLuint t;
        glGenTextures(1, &t);
        texture_ = t;
        glBindTexture(GL_TEXTURE_2D, texture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (w != texW_ || h != texH_) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, frame_.data());
        texW_ = w;
        texH_ = h;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, frame_.data());
    }
}

void VmScreen::releaseKeyboard() {
    for (auto& [keysym, xt] : heldKeys_) client_.sendKey(keysym, xt, false);  // no stuck Ctrl in the guest
    heldKeys_.clear();
    if (focused_ == this) focused_ = nullptr;
}

void VmScreen::draw(const ImVec2& size) {
    upload();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 end(origin.x + size.x, origin.y + size.y);

    ImGui::InvisibleButton("##vmscreen", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    if (ImGui::IsItemActivated()) focused_ = this;
    if (focused_ == this && !hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) releaseKeyboard();

    dl->AddRectFilled(origin, end, IM_COL32(0, 0, 0, 255), 6);

    if (client_.state() != RfbClient::State::Connected || texW_ == 0) {
        const char* msg = client_.state() == RfbClient::State::Disconnected ? "Display disconnected" : "Connecting to display...";
        ImVec2 ts = ImGui::CalcTextSize(msg);
        dl->AddText(ImVec2(origin.x + (size.x - ts.x) / 2, origin.y + (size.y - ts.y) / 2), colors::Muted, msg);
        return;
    }

    // Fit the guest screen into the available area, keeping its aspect ratio.
    float scale = std::min(size.x / texW_, size.y / texH_);
    ImVec2 img(texW_ * scale, texH_ * scale);
    ImVec2 p0(origin.x + (size.x - img.x) / 2, origin.y + (size.y - img.y) / 2);
    ImVec2 p1(p0.x + img.x, p0.y + img.y);
    dl->AddImage(static_cast<ImTextureID>(static_cast<intptr_t>(texture_)), p0, p1);

    if (focused_ == this) {
        dl->AddRect(ImVec2(p0.x - 1, p0.y - 1), ImVec2(p1.x + 1, p1.y + 1), colors::Accent, 0, 0, 2);
    } else if (hovered) {
        const char* hint = "Click to control this VM  -  Ctrl+Alt releases the keyboard";
        ImVec2 ts = ImGui::CalcTextSize(hint);
        ImVec2 hp(p0.x + (img.x - ts.x) / 2, p1.y - ts.y - 16);
        dl->AddRectFilled(ImVec2(hp.x - 10, hp.y - 5), ImVec2(hp.x + ts.x + 10, hp.y + ts.y + 5), IM_COL32(0, 0, 0, 180), 6);
        dl->AddText(hp, IM_COL32(255, 255, 255, 255), hint);
    }

    if (hovered || active) {
        ImGuiIO& io = ImGui::GetIO();
        int x = static_cast<int>((io.MousePos.x - p0.x) / scale);
        int y = static_cast<int>((io.MousePos.y - p0.y) / scale);
        x = std::clamp(x, 0, texW_ - 1);
        y = std::clamp(y, 0, texH_ - 1);
        std::uint8_t buttons = 0;
        if (focused_ == this) {  // the first click only focuses the screen
            if (io.MouseDown[0]) buttons |= 1;
            if (io.MouseDown[2]) buttons |= 2;
            if (io.MouseDown[1]) buttons |= 4;
        }
        if (x != lastX_ || y != lastY_ || buttons != lastButtons_) {
            client_.sendPointer(x, y, buttons);
            lastX_ = x;
            lastY_ = y;
            lastButtons_ = buttons;
        }
        if (focused_ == this && io.MouseWheel != 0) {
            std::uint8_t wheel = io.MouseWheel > 0 ? 8 : 16;
            client_.sendPointer(x, y, buttons | wheel);
            client_.sendPointer(x, y, buttons);
        }
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_None);  // the guest draws its own cursor
    }
}

void VmScreen::handleKey(int key, int scancode, int action, int mods) {
    bool down = action != GLFW_RELEASE;
    if (key == GLFW_KEY_LEFT_CONTROL || key == GLFW_KEY_RIGHT_CONTROL) ctrlDown_ = down;
    if (key == GLFW_KEY_LEFT_ALT || key == GLFW_KEY_RIGHT_ALT) altDown_ = down;

    // Host shortcuts, as in VMware Workstation.
    if (down && key == GLFW_KEY_ENTER && ctrlDown_ && altDown_) {
        if (fullscreenShortcut_) fullscreenShortcut_();
        return;
    }
    VmScreen* screen = focused_;
    if (!screen) return;
    if (ctrlDown_ && altDown_) {
        screen->releaseKeyboard();
        return;
    }
    std::uint32_t xt = scancodeToXt(scancode);
    std::uint32_t keysym = glfwKeyToKeysym(key, (mods & GLFW_MOD_SHIFT) != 0);
    if (xt == 0 && keysym == 0) return;
    if (down) screen->heldKeys_.insert({keysym, xt});
    else screen->heldKeys_.erase({keysym, xt});
    screen->client_.sendKey(keysym, xt, down);
}

}  // namespace eduvm::app
