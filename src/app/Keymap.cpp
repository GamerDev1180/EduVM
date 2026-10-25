#include "app/Keymap.hpp"

#include <GLFW/glfw3.h>

namespace eduvm::app {

std::uint32_t scancodeToXt(int sc) {
    if (sc <= 0) return 0;
#if defined(_WIN32)
    // Win32 scancodes are already XT set 1; GLFW marks extended keys with 0x100.
    std::uint32_t code = static_cast<std::uint32_t>(sc) & 0xFF;
    if (code == 0 || code >= 0x80) return 0;
    if (sc == 0x145) return 0x45;  // GLFW reports NumLock as extended; XT does not
    return (sc & 0x100) ? (code | 0x80) : code;
#elif defined(__linux__)
    // X11/Wayland keycodes are evdev codes + 8. evdev 1..88 equal XT set 1.
    int ev = sc - 8;
    if (ev >= 1 && ev <= 88) return static_cast<std::uint32_t>(ev);
    switch (ev) {
        case 96: return 0x9C;   // keypad enter
        case 97: return 0x9D;   // right ctrl
        case 98: return 0xB5;   // keypad /
        case 99: return 0xB7;   // print screen
        case 100: return 0xB8;  // right alt / AltGr
        case 102: return 0xC7;  // home
        case 103: return 0xC8;  // up
        case 104: return 0xC9;  // page up
        case 105: return 0xCB;  // left
        case 106: return 0xCD;  // right
        case 107: return 0xCF;  // end
        case 108: return 0xD0;  // down
        case 109: return 0xD1;  // page down
        case 110: return 0xD2;  // insert
        case 111: return 0xD3;  // delete
        case 125: return 0xDB;  // left super
        case 126: return 0xDC;  // right super
        case 127: return 0xDD;  // menu
        default: return 0;
    }
#else
    return 0;  // macOS: fall back to keysyms
#endif
}

std::uint32_t glfwKeyToKeysym(int key, bool shift) {
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) return static_cast<std::uint32_t>((shift ? 'A' : 'a') + (key - GLFW_KEY_A));
    if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9 && !shift) return static_cast<std::uint32_t>('0' + (key - GLFW_KEY_0));
    if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F12) return 0xffbe + static_cast<std::uint32_t>(key - GLFW_KEY_F1);
    if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9) return 0xffb0 + static_cast<std::uint32_t>(key - GLFW_KEY_KP_0);
    switch (key) {
        case GLFW_KEY_SPACE: return ' ';
        case GLFW_KEY_APOSTROPHE: return shift ? '"' : '\'';
        case GLFW_KEY_COMMA: return shift ? '<' : ',';
        case GLFW_KEY_MINUS: return shift ? '_' : '-';
        case GLFW_KEY_PERIOD: return shift ? '>' : '.';
        case GLFW_KEY_SLASH: return shift ? '?' : '/';
        case GLFW_KEY_SEMICOLON: return shift ? ':' : ';';
        case GLFW_KEY_EQUAL: return shift ? '+' : '=';
        case GLFW_KEY_LEFT_BRACKET: return shift ? '{' : '[';
        case GLFW_KEY_BACKSLASH: return shift ? '|' : '\\';
        case GLFW_KEY_RIGHT_BRACKET: return shift ? '}' : ']';
        case GLFW_KEY_GRAVE_ACCENT: return shift ? '~' : '`';
        case GLFW_KEY_ESCAPE: return 0xff1b;
        case GLFW_KEY_ENTER: return 0xff0d;
        case GLFW_KEY_TAB: return 0xff09;
        case GLFW_KEY_BACKSPACE: return 0xff08;
        case GLFW_KEY_INSERT: return 0xff63;
        case GLFW_KEY_DELETE: return 0xffff;
        case GLFW_KEY_RIGHT: return 0xff53;
        case GLFW_KEY_LEFT: return 0xff51;
        case GLFW_KEY_DOWN: return 0xff54;
        case GLFW_KEY_UP: return 0xff52;
        case GLFW_KEY_PAGE_UP: return 0xff55;
        case GLFW_KEY_PAGE_DOWN: return 0xff56;
        case GLFW_KEY_HOME: return 0xff50;
        case GLFW_KEY_END: return 0xff57;
        case GLFW_KEY_CAPS_LOCK: return 0xffe5;
        case GLFW_KEY_LEFT_SHIFT: return 0xffe1;
        case GLFW_KEY_RIGHT_SHIFT: return 0xffe2;
        case GLFW_KEY_LEFT_CONTROL: return 0xffe3;
        case GLFW_KEY_RIGHT_CONTROL: return 0xffe4;
        case GLFW_KEY_LEFT_ALT: return 0xffe9;
        case GLFW_KEY_RIGHT_ALT: return 0xffea;
        case GLFW_KEY_LEFT_SUPER: return 0xffeb;
        case GLFW_KEY_RIGHT_SUPER: return 0xffec;
        case GLFW_KEY_KP_ENTER: return 0xff8d;
        case GLFW_KEY_KP_ADD: return 0xffab;
        case GLFW_KEY_KP_SUBTRACT: return 0xffad;
        case GLFW_KEY_KP_MULTIPLY: return 0xffaa;
        case GLFW_KEY_KP_DIVIDE: return 0xffaf;
        case GLFW_KEY_KP_DECIMAL: return 0xffae;
        default: break;
    }
    if (shift && key >= GLFW_KEY_0 && key <= GLFW_KEY_9) return static_cast<std::uint32_t>(")!@#$%^&*("[key - GLFW_KEY_0]);
    return 0;
}

}  // namespace eduvm::app
