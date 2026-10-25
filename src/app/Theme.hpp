#pragma once

#include <imgui.h>

#include <string>

namespace eduvm::app {

struct Fonts {
    ImFont* regular = nullptr;
    ImFont* bold = nullptr;
    ImFont* heading = nullptr;
    ImFont* mono = nullptr;
};
extern Fonts g_fonts;

namespace colors {
inline constexpr ImU32 Accent = IM_COL32(79, 140, 255, 255);
inline constexpr ImU32 Success = IM_COL32(64, 192, 120, 255);
inline constexpr ImU32 Warning = IM_COL32(240, 176, 64, 255);
inline constexpr ImU32 Danger = IM_COL32(232, 88, 88, 255);
inline constexpr ImU32 Muted = IM_COL32(120, 128, 140, 255);
inline constexpr ImU32 Panel = IM_COL32(30, 33, 40, 255);
inline constexpr ImU32 PanelRaised = IM_COL32(39, 43, 52, 255);
}  // namespace colors

void loadFonts(float scale);
void applyTheme(float scale);

// Small reusable widgets.
void statusDot(ImU32 color);                     // filled circle aligned with text
void statusIcon(bool ok);                        // circle with check mark or cross
void spinner(float radius);                      // indeterminate progress
void chip(const std::string& text, ImU32 color); // rounded label, e.g. "Running"
bool primaryButton(const char* label, const ImVec2& size = ImVec2(0, 0));
bool dangerButton(const char* label, const ImVec2& size = ImVec2(0, 0));
void mutedText(const std::string& text);
void heading(const std::string& text);
// Wrapped text that also respects explicit newlines.
void wrappedText(const std::string& text);
// Shortens text with "..." so it fits in maxWidth with the current font.
std::string ellipsize(const std::string& text, float maxWidth);

enum class TileIcon { Plus, Import, Check };
// Large clickable card used on the home page.
bool tile(const char* id, TileIcon icon, const std::string& title, const std::string& subtitle, const ImVec2& size);
// Small colored badge with the guest OS initials ("WIN", "LNX", "VM").
void osBadge(const std::string& os, float size);
// Centers the next window (dialog) in the main window when it appears.
void centerNextWindow();
// "2 GB", "1.5 GB", "768 MB"
std::string formatMemory(int mb);
// Text that looks and acts like a hyperlink (VMware-style "Power on this virtual machine").
bool linkButton(const std::string& label);

}  // namespace eduvm::app
