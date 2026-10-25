#pragma once

#include <cstdint>

namespace eduvm::app {

// GLFW platform scancode -> XT scancode (extended keys as 0x80|code), the
// format QEMU's extended key event expects. Returns 0 when unknown.
std::uint32_t scancodeToXt(int glfwScancode);

// GLFW key -> X11 keysym, used when the server lacks extended key events.
std::uint32_t glfwKeyToKeysym(int glfwKey, bool shift);

}  // namespace eduvm::app
