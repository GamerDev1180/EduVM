#include "app/Theme.hpp"

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace eduvm::app {

Fonts g_fonts;

namespace {

ImFont* tryLoad(const std::vector<const char*>& candidates, float size) {
    ImGuiIO& io = ImGui::GetIO();
    static const ImWchar ranges[] = {0x0020, 0x00FF, 0x0100, 0x017F, 0x2010, 0x205E, 0};  // Latin + punctuation
    for (const char* path : candidates) {
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) return io.Fonts->AddFontFromFileTTF(path, size, nullptr, ranges);
    }
    return nullptr;
}

ImVec4 rgba(int r, int g, int b, float a = 1.0f) { return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a); }

}  // namespace

void loadFonts(float scale) {
    ImGuiIO& io = ImGui::GetIO();
    const float base = std::round(16.0f * scale);
    g_fonts.regular = tryLoad({"C:\\Windows\\Fonts\\segoeui.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                               "/System/Library/Fonts/SFNS.ttf"},
                              base);
    if (!g_fonts.regular) {
        ImFontConfig cfg;
        cfg.SizePixels = base;
        g_fonts.regular = io.Fonts->AddFontDefault(&cfg);
    }
    g_fonts.bold = tryLoad({"C:\\Windows\\Fonts\\segoeuib.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"}, base);
    g_fonts.heading = tryLoad({"C:\\Windows\\Fonts\\segoeuib.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"},
                              std::round(22.0f * scale));
    g_fonts.mono = tryLoad({"C:\\Windows\\Fonts\\consola.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"},
                           std::round(15.0f * scale));
    if (!g_fonts.bold) g_fonts.bold = g_fonts.regular;
    if (!g_fonts.heading) g_fonts.heading = g_fonts.bold;
    if (!g_fonts.mono) g_fonts.mono = g_fonts.regular;
}

void applyTheme(float scale) {
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    s.WindowPadding = ImVec2(14, 12);
    s.FramePadding = ImVec2(10, 6);
    s.ItemSpacing = ImVec2(10, 8);
    s.ItemInnerSpacing = ImVec2(8, 6);
    s.ScrollbarSize = 12;
    s.GrabMinSize = 10;
    s.WindowRounding = 8;
    s.ChildRounding = 8;
    s.FrameRounding = 6;
    s.PopupRounding = 8;
    s.ScrollbarRounding = 6;
    s.GrabRounding = 6;
    s.TabRounding = 6;
    s.WindowBorderSize = 0;
    s.ChildBorderSize = 1;
    s.PopupBorderSize = 1;
    s.FrameBorderSize = 0;
    s.SeparatorTextBorderSize = 1;
    s.ScaleAllSizes(scale);

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = rgba(230, 233, 239);
    c[ImGuiCol_TextDisabled] = rgba(120, 128, 140);
    c[ImGuiCol_WindowBg] = rgba(22, 24, 29);
    c[ImGuiCol_ChildBg] = rgba(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = rgba(30, 33, 40);
    c[ImGuiCol_Border] = rgba(48, 53, 64);
    c[ImGuiCol_FrameBg] = rgba(39, 43, 52);
    c[ImGuiCol_FrameBgHovered] = rgba(48, 53, 64);
    c[ImGuiCol_FrameBgActive] = rgba(56, 62, 75);
    c[ImGuiCol_TitleBg] = rgba(22, 24, 29);
    c[ImGuiCol_TitleBgActive] = rgba(30, 33, 40);
    c[ImGuiCol_MenuBarBg] = rgba(18, 20, 24);
    c[ImGuiCol_ScrollbarBg] = rgba(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = rgba(56, 62, 75);
    c[ImGuiCol_ScrollbarGrabHovered] = rgba(70, 77, 92);
    c[ImGuiCol_ScrollbarGrabActive] = rgba(84, 92, 110);
    c[ImGuiCol_CheckMark] = rgba(79, 140, 255);
    c[ImGuiCol_SliderGrab] = rgba(79, 140, 255);
    c[ImGuiCol_SliderGrabActive] = rgba(110, 160, 255);
    c[ImGuiCol_Button] = rgba(44, 49, 60);
    c[ImGuiCol_ButtonHovered] = rgba(56, 62, 75);
    c[ImGuiCol_ButtonActive] = rgba(66, 73, 88);
    c[ImGuiCol_Header] = rgba(44, 49, 60);
    c[ImGuiCol_HeaderHovered] = rgba(52, 58, 71);
    c[ImGuiCol_HeaderActive] = rgba(79, 140, 255, 0.35f);
    c[ImGuiCol_Separator] = rgba(48, 53, 64);
    c[ImGuiCol_Tab] = rgba(30, 33, 40);
    c[ImGuiCol_TabHovered] = rgba(56, 62, 75);
    c[ImGuiCol_TabSelected] = rgba(44, 49, 60);
    c[ImGuiCol_TabSelectedOverline] = rgba(79, 140, 255);
    c[ImGuiCol_TableHeaderBg] = rgba(30, 33, 40);
    c[ImGuiCol_TableBorderLight] = rgba(40, 44, 54);
    c[ImGuiCol_TableBorderStrong] = rgba(48, 53, 64);
    c[ImGuiCol_TableRowBgAlt] = rgba(255, 255, 255, 0.02f);
    c[ImGuiCol_PlotHistogram] = rgba(64, 192, 120);
    c[ImGuiCol_ModalWindowDimBg] = rgba(0, 0, 0, 0.55f);
    c[ImGuiCol_TextSelectedBg] = rgba(79, 140, 255, 0.35f);
}

void statusDot(ImU32 color) {
    float h = ImGui::GetTextLineHeight();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + h * 0.35f, p.y + h * 0.5f), h * 0.22f, color);
    ImGui::Dummy(ImVec2(h * 0.7f, h));
}

void statusIcon(bool ok) {
    float h = ImGui::GetTextLineHeight();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 c(p.x + h * 0.5f, p.y + h * 0.5f);
    float r = h * 0.42f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddCircleFilled(c, r, ok ? colors::Success : colors::PanelRaised);
    if (ok) {
        ImVec2 pts[3] = {{c.x - r * 0.45f, c.y + r * 0.02f}, {c.x - r * 0.1f, c.y + r * 0.38f}, {c.x + r * 0.5f, c.y - r * 0.35f}};
        dl->AddPolyline(pts, 3, IM_COL32(255, 255, 255, 255), 0, h * 0.11f);
    } else {
        dl->AddCircle(c, r, colors::Muted, 0, h * 0.07f);
    }
    ImGui::Dummy(ImVec2(h, h));
}

void spinner(float radius) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    float t = static_cast<float>(ImGui::GetTime());
    ImVec2 c(p.x + radius, p.y + ImGui::GetTextLineHeight() * 0.5f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PathArcTo(c, radius, t * 6.0f, t * 6.0f + 4.2f, 24);
    dl->PathStroke(colors::Accent, 0, radius * 0.3f);
    ImGui::Dummy(ImVec2(radius * 2, ImGui::GetTextLineHeight()));
}

void chip(const std::string& text, ImU32 color) {
    ImVec2 size = ImGui::CalcTextSize(text.c_str());
    ImVec2 pad(8, 2);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImU32 bg = (color & 0x00FFFFFF) | (60u << 24);
    ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + size.x + pad.x * 2, p.y + size.y + pad.y * 2), bg, size.y);
    ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + pad.x, p.y + pad.y), color, text.c_str());
    ImGui::Dummy(ImVec2(size.x + pad.x * 2, size.y + pad.y * 2));
}

static bool coloredButton(const char* label, const ImVec2& size, ImVec4 base) {
    ImGui::PushStyleColor(ImGuiCol_Button, base);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(base.x * 1.12f, base.y * 1.12f, base.z * 1.12f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(base.x * 0.9f, base.y * 0.9f, base.z * 0.9f, 1));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return pressed;
}

bool primaryButton(const char* label, const ImVec2& size) { return coloredButton(label, size, rgba(64, 120, 235)); }
bool dangerButton(const char* label, const ImVec2& size) { return coloredButton(label, size, rgba(190, 64, 64)); }

void mutedText(const std::string& text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

void heading(const std::string& text) {
    ImGui::PushFont(g_fonts.heading);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopFont();
}

std::string ellipsize(const std::string& text, float maxWidth) {
    if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth) return text;
    std::string s = text;
    while (!s.empty() && ImGui::CalcTextSize((s + "...").c_str()).x > maxWidth) {
        s.pop_back();
        while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) s.pop_back();  // keep UTF-8 intact
    }
    return s + "...";
}

bool tile(const char* id, TileIcon icon, const std::string& title, const std::string& subtitle, const ImVec2& size) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(id, size);
    bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), hovered ? IM_COL32(46, 51, 63, 255) : colors::PanelRaised, 10);
    if (hovered) dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), colors::Accent, 10, 0, 1.5f);

    const float r = size.y * 0.16f;
    ImVec2 c(p.x + size.y * 0.28f, p.y + size.y * 0.5f);
    dl->AddCircleFilled(c, r * 1.35f, IM_COL32(79, 140, 255, 40));
    const ImU32 ic = colors::Accent;
    const float t = r * 0.18f;
    switch (icon) {
        case TileIcon::Plus:
            dl->AddLine(ImVec2(c.x - r * 0.6f, c.y), ImVec2(c.x + r * 0.6f, c.y), ic, t);
            dl->AddLine(ImVec2(c.x, c.y - r * 0.6f), ImVec2(c.x, c.y + r * 0.6f), ic, t);
            break;
        case TileIcon::Import:
            dl->AddLine(ImVec2(c.x, c.y - r * 0.65f), ImVec2(c.x, c.y + r * 0.35f), ic, t);
            dl->AddLine(ImVec2(c.x - r * 0.4f, c.y - r * 0.05f), ImVec2(c.x, c.y + r * 0.35f), ic, t);
            dl->AddLine(ImVec2(c.x + r * 0.4f, c.y - r * 0.05f), ImVec2(c.x, c.y + r * 0.35f), ic, t);
            dl->AddLine(ImVec2(c.x - r * 0.65f, c.y + r * 0.65f), ImVec2(c.x + r * 0.65f, c.y + r * 0.65f), ic, t);
            break;
        case TileIcon::Check: {
            ImVec2 pts[3] = {{c.x - r * 0.55f, c.y}, {c.x - r * 0.12f, c.y + r * 0.45f}, {c.x + r * 0.6f, c.y - r * 0.45f}};
            dl->AddPolyline(pts, 3, ic, 0, t);
            break;
        }
    }

    float textX = p.x + size.y * 0.56f;
    float wrap = p.x + size.x - textX - 12;
    ImGui::PushFont(g_fonts.bold);
    float titleH = ImGui::GetTextLineHeight();
    ImVec2 subSize = ImGui::CalcTextSize(subtitle.c_str(), nullptr, false, wrap);
    float y = p.y + (size.y - titleH - subSize.y - 4) / 2;
    dl->AddText(ImVec2(textX, y), ImGui::GetColorU32(ImGuiCol_Text), title.c_str());
    ImGui::PopFont();
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(textX, y + titleH + 4), colors::Muted, subtitle.c_str(), nullptr, wrap);
    return pressed;
}

void osBadge(const std::string& os, float size) {
    const char* label = os == "windows" ? "WIN" : os == "linux" ? "LNX" : "VM";
    ImU32 color = os == "windows" ? IM_COL32(0, 120, 212, 255) : os == "linux" ? IM_COL32(233, 128, 36, 255) : IM_COL32(110, 118, 132, 255);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + size, p.y + size), color, size * 0.22f);
    ImGui::PushFont(g_fonts.bold);
    float fs = size * 0.36f;
    ImVec2 ts = g_fonts.bold->CalcTextSizeA(fs, FLT_MAX, 0, label);
    dl->AddText(g_fonts.bold, fs, ImVec2(p.x + (size - ts.x) / 2, p.y + (size - ts.y) / 2), IM_COL32(255, 255, 255, 255), label);
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(size, size));
}

void centerNextWindow() {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
}

std::string formatMemory(int mb) {
    if (mb < 1024) return std::to_string(mb) + " MB";
    if (mb % 1024 == 0) return std::to_string(mb / 1024) + " GB";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f GB", mb / 1024.0);
    return buf;
}

bool linkButton(const std::string& label) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(IM_COL32(110, 160, 255, 255)));
    ImGui::TextUnformatted(label.c_str());
    ImGui::PopStyleColor();
    bool hovered = ImGui::IsItemHovered();
    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x, b.y), b, IM_COL32(110, 160, 255, 255));
    }
    return hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
}

void wrappedText(const std::string& text) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
}

}  // namespace eduvm::app
