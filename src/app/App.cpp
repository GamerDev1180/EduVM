#include "app/App.hpp"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>

#include "app/Platform.hpp"
#include "app/Theme.hpp"
#include "core/Paths.hpp"
#include "core/Strings.hpp"
#include "eduvm/Version.hpp"

namespace eduvm::app {

namespace {

Settings loadSettingsOrDefault() {
    try {
        return Settings::load();
    } catch (const std::exception&) {
        return Settings{};  // a corrupt settings file must never stop the app from starting
    }
}

ImU32 stateColor(VmState s) {
    switch (s) {
        case VmState::Running: return colors::Success;
        case VmState::Paused: return colors::Warning;
        case VmState::Unknown: return colors::Accent;
        case VmState::Stopped: return colors::Muted;
    }
    return colors::Muted;
}

const char* stateText(VmState s) {
    switch (s) {
        case VmState::Running: return "Running";
        case VmState::Paused: return "Suspended";
        case VmState::Unknown: return "Busy";
        case VmState::Stopped: return "Powered off";
    }
    return "";
}

// Job labels are "kind:subject"; this turns them into status bar text.
std::string describeJob(const std::string& label) {
    static const std::pair<const char*, const char*> kinds[] = {
        {"start:", "Powering on "}, {"stop:", "Shutting down "}, {"snap:", "Snapshot on "},
        {"newvm:", "Creating "},    {"clone:", "Cloning "},      {"expand:", "Expanding disk of "},
        {"diag", "Checking this computer"},
    };
    for (auto& [prefix, text] : kinds)
        if (startsWith(label, prefix)) return text + label.substr(std::string(prefix).size());
    return {};
}

}  // namespace

App::App(float uiScale, GLFWwindow* window)
    : scale_(uiScale),
      window_(window),
      settings_(loadSettingsOrDefault()),
      repo_(vmsDir()),
      host_(HostInfo::detect()),
      backend_(std::make_shared<QemuBackend>(settings_, repo_, host_)),
      monitor_(repo_, [this] { return backend(); }) {
    VmScreen::setFullscreenShortcut([this] { toggleFullscreen(); });
    monitor_.start();
    runDiagnosticsAsync(true);
    if (settings_.checkForUpdates) checkForUpdatesAsync(false);
}

App::~App() {
    VmScreen::setFullscreenShortcut({});
    jobs_.waitAll();
    screens_.clear();
    monitor_.stop();
}

std::shared_ptr<const QemuBackend> App::backend() const {
    std::lock_guard lock(backendMutex_);
    return backend_;
}

void App::rebuildBackend() {
    auto fresh = std::make_shared<QemuBackend>(settings_, repo_, host_);
    std::lock_guard lock(backendMutex_);
    backend_ = std::move(fresh);
}

void App::toast(std::string text, bool error) {
    toasts_.push_back({std::move(text), error, ImGui::GetTime() + (error ? 10.0 : 5.0)});
}

void App::confirm(std::string title, std::string text, std::string action, std::function<void()> onConfirm) {
    confirmTitle_ = std::move(title);
    confirmText_ = std::move(text);
    confirmAction_ = std::move(action);
    confirmCallback_ = std::move(onConfirm);
    showConfirm_ = true;
}

const VmStatus* App::findStatus(const std::string& name) const {
    for (auto& st : statuses_)
        if (st.config.name == name) return &st;
    return nullptr;
}

std::string App::uniqueVmName(const std::string& displayName) const {
    std::string base = slugify(displayName);
    std::string name = base;
    for (int i = 2; repo_.exists(name); ++i) name = base + "-" + std::to_string(i);
    return name;
}

bool App::requestExit() {
    if (exitApproved_) return true;
    bool anyRunning = std::any_of(statuses_.begin(), statuses_.end(), [](const VmStatus& v) { return v.state != VmState::Stopped; });
    if (!anyRunning) return true;
    if (fullscreen_) toggleFullscreen();
    showExit_ = true;
    return false;
}

bool App::wantsContinuousFrames() const {
    if (jobs_.any() || !toasts_.empty()) return true;
    for (auto& [name, screen] : screens_)
        if (screen->state() == RfbClient::State::Connected || screen->state() == RfbClient::State::Connecting) return true;
    return false;
}

// Full screen shows only the selected VM's display, like VMware's full screen
// mode. Toggled with Ctrl+Alt+Enter, the View menu or the toolbar.
void App::toggleFullscreen() {
    if (fullscreen_) {
        glfwSetWindowMonitor(window_, nullptr, windowedX_, windowedY_, windowedW_, windowedH_, 0);
        fullscreen_ = false;
        return;
    }
    const VmStatus* vm = page_ == Page::Vm ? findStatus(selectedVm_) : nullptr;
    auto screen = vm ? screens_.find(vm->config.name) : screens_.end();
    if (!vm || vm->state == VmState::Stopped || screen == screens_.end()) {
        toast("Full screen is available while a VM's console is shown.");
        return;
    }
    glfwGetWindowPos(window_, &windowedX_, &windowedY_);
    glfwGetWindowSize(window_, &windowedW_, &windowedH_);
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    glfwSetWindowMonitor(window_, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    screen->second->focusKeyboard();
    fullscreen_ = true;
}

// ------------------------------------------------------------------ frame

void App::frame() {
    for (auto& err : jobs_.poll()) toast(err, true);
    statuses_ = monitor_.snapshot();
    pruneScreens();

    if (!VmScreen::anyHasKeyboard() && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N, false)) showWizard_ = true;

    if (fullscreen_) {
        drawFullscreen();
    } else {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(s(8), s(8)));
        ImGui::Begin("##root", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::PopStyleVar();
        drawMenuBar();

        const float statusH = ImGui::GetFrameHeight() + s(4);
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float bodyH = avail.y - statusH;
        const float sidebarW = s(270);

        if (showLibrary_) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(colors::Panel));
            ImGui::BeginChild("##sidebar", ImVec2(sidebarW, bodyH), ImGuiChildFlags_AlwaysUseWindowPadding);
            drawSidebar();
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::SameLine();
        }
        ImGui::BeginChild("##main", ImVec2(0, bodyH), ImGuiChildFlags_AlwaysUseWindowPadding);
        if (page_ == Page::Vm) drawVmPage();
        else drawHome();
        ImGui::EndChild();

        drawStatusBar();
        ImGui::End();
    }

    drawNewVmWizard();
    drawVmSettingsDialog();
    drawImportDialog();
    drawCloneDialog();
    drawPreferencesDialog();
    drawDiagnosticsDialog();
    drawAboutDialog();
    drawConfirmDialog();
    drawExitDialog();
    drawToasts();
}

// --------------------------------------------------------------- menu bar

void App::drawMenuBar() {
    if (!ImGui::BeginMenuBar()) return;
    const VmStatus* vm = page_ == Page::Vm ? findStatus(selectedVm_) : nullptr;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Virtual Machine...", "Ctrl+N")) showWizard_ = true;
        if (ImGui::MenuItem("Import Disk Image...")) showImport_ = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Open VM Folder")) openFolder(vmsDir());
        if (ImGui::MenuItem("Preferences...")) {
            prefsEdit_ = settings_;
            showPreferences_ = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) glfwSetWindowShouldClose(window_, GLFW_TRUE);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("VM", vm != nullptr)) {
        const std::string name = vm->config.name;
        const bool stopped = vm->state == VmState::Stopped;
        if (ImGui::MenuItem("Power On", nullptr, false, stopped)) powerOn(name);
        if (ImGui::MenuItem("Shut Down Guest", nullptr, false, vm->state == VmState::Running)) powerOff(name, false);
        if (ImGui::MenuItem(vm->state == VmState::Paused ? "Resume" : "Suspend", nullptr, false, !stopped))
            setPaused(name, vm->state != VmState::Paused);
        if (ImGui::MenuItem("Power Off", nullptr, false, !stopped)) powerOff(name, true);
        ImGui::Separator();
        auto screen = screens_.find(name);
        if (ImGui::MenuItem("Send Ctrl+Alt+Del", nullptr, false, screen != screens_.end())) screen->second->sendCtrlAltDel();
        if (ImGui::MenuItem("Snapshots")) {
            vmTab_ = VmTab::Snapshots;
            forceTab_ = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Settings...")) openVmSettings(vm->config);
        if (ImGui::MenuItem("Clone...", nullptr, false, stopped)) {
            cloneSource_ = name;
            cloneName_ = vmTitle(vm->config) + " (clone)";
            showClone_ = true;
        }
        if (ImGui::MenuItem("Delete from Disk", nullptr, false, stopped)) deleteVm(vm->config);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Home")) page_ = Page::Home;
        ImGui::MenuItem("Library", nullptr, &showLibrary_);
        if (ImGui::MenuItem("Full Screen", "Ctrl+Alt+Enter", false, vm && vm->state != VmState::Stopped)) toggleFullscreen();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("System Check...")) {
            showDiagnostics_ = true;
            runDiagnosticsAsync();
        }
        if (ImGui::MenuItem("Check for Updates")) checkForUpdatesAsync(true);
        ImGui::Separator();
        if (ImGui::MenuItem("About EduVM")) showAbout_ = true;
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}

// ---------------------------------------------------------------- sidebar

void App::drawSidebar() {
    ImGui::PushFont(g_fonts.bold);
    ImGui::TextUnformatted("LIBRARY");
    ImGui::PopFont();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##search", "Search virtual machines", &librarySearch_);
    ImGui::Spacing();

    ImGui::BeginChild("##list", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - s(4)));
    if (ImGui::Selectable("Home", page_ == Page::Home)) page_ = Page::Home;
    ImGui::Spacing();
    ImGui::SeparatorText("My Computer");

    const std::string query = toLower(trim(librarySearch_));
    const float rowH = ImGui::GetTextLineHeight() * 2.4f;
    const float badge = rowH * 0.72f;
    int shown = 0;
    for (auto& st : statuses_) {
        const std::string title = vmTitle(st.config);
        if (!query.empty() && toLower(title).find(query) == std::string::npos) continue;
        ++shown;
        ImGui::PushID(st.config.name.c_str());
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        if (ImGui::Selectable("##vm", page_ == Page::Vm && selectedVm_ == st.config.name, 0, ImVec2(0, rowH))) selectVm(st.config.name);
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && st.state == VmState::Stopped)
            powerOn(st.config.name);
        if (ImGui::BeginPopupContextItem("##ctx")) {
            selectVm(st.config.name);
            if (ImGui::MenuItem("Power On", nullptr, false, st.state == VmState::Stopped)) powerOn(st.config.name);
            if (ImGui::MenuItem("Power Off", nullptr, false, st.state != VmState::Stopped)) powerOff(st.config.name, true);
            if (ImGui::MenuItem("Settings...")) openVmSettings(st.config);
            ImGui::EndPopup();
        }
        ImVec2 after = ImGui::GetCursorScreenPos();

        ImGui::SetCursorScreenPos(ImVec2(p.x + s(4), p.y + (rowH - badge) / 2));
        osBadge(toString(st.config.guestOs), badge);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float tx = p.x + s(4) + badge + s(10);
        float lh = ImGui::GetTextLineHeight();
        float ty = p.y + (rowH - lh * 2 - s(2)) / 2;
        ImGui::PushFont(g_fonts.bold);
        dl->AddText(ImVec2(tx, ty), ImGui::GetColorU32(ImGuiCol_Text), ellipsize(title, p.x + w - tx - s(4)).c_str());
        ImGui::PopFont();
        float r = lh * 0.2f;
        dl->AddCircleFilled(ImVec2(tx + r, ty + lh + s(2) + lh * 0.5f), r, stateColor(st.state));
        dl->AddText(ImVec2(tx + r * 2 + s(6), ty + lh + s(2)), colors::Muted, stateText(st.state));
        ImGui::SetCursorScreenPos(after);
        ImGui::PopID();
    }
    if (statuses_.empty()) mutedText("No virtual machines yet.");
    else if (shown == 0) mutedText("No matches.");
    ImGui::EndChild();

    if (primaryButton("New Virtual Machine", ImVec2(-1, 0))) showWizard_ = true;
}

// ------------------------------------------------------------------- home

void App::drawHome() {
    ImGui::Dummy(ImVec2(0, s(8)));
    heading("EduVM");
    mutedText("Create and run virtual machines on this computer.");
    ImGui::Spacing();

    bool blocking = std::any_of(diagnostics_.begin(), diagnostics_.end(), [](const DiagnosticItem& d) { return !d.ok && !d.optional; });
    if (blocking) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.35f, 0.22f, 0.08f, 0.6f));
        ImGui::BeginChild("##setup", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PushFont(g_fonts.bold);
        ImGui::TextUnformatted("This computer is not ready to run virtual machines yet");
        ImGui::PopFont();
        for (auto& d : diagnostics_)
            if (!d.ok && !d.optional) wrappedText("- " + d.name + ": " + d.detail);
        if (ImGui::Button("Open System Check")) {
            showDiagnostics_ = true;
            runDiagnosticsAsync();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float tileW = (ImGui::GetContentRegionAvail().x - spacing * 2) / 3;
    const ImVec2 tileSize(tileW, s(96));
    if (tile("##new", TileIcon::Plus, "Create a New Virtual Machine", "Install an operating system from an ISO file", tileSize))
        showWizard_ = true;
    ImGui::SameLine();
    if (tile("##import", TileIcon::Import, "Import a Disk Image", "Use an existing .vmdk, .vdi, .vhdx or .qcow2 disk", tileSize))
        showImport_ = true;
    ImGui::SameLine();
    if (tile("##check", TileIcon::Check, "System Check", "See if this computer is ready to run VMs", tileSize)) {
        showDiagnostics_ = true;
        runDiagnosticsAsync();
    }

    ImGui::Dummy(ImVec2(0, s(8)));
    ImGui::SeparatorText("Virtual machines");
    if (statuses_.empty()) {
        mutedText("You don't have any virtual machines yet. Create one to get started.");
        return;
    }

    const float cardW = s(300), cardH = s(128);
    const int columns = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + spacing) / (cardW + spacing)));
    int i = 0;
    for (auto& st : statuses_) {
        ImGui::PushID(st.config.name.c_str());
        if (i++ % columns != 0) ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(colors::PanelRaised));
        ImGui::BeginChild("##card", ImVec2(cardW, cardH), ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        osBadge(toString(st.config.guestOs), s(36));
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::PushFont(g_fonts.bold);
        ImGui::TextUnformatted(ellipsize(vmTitle(st.config), cardW - s(80)).c_str());
        ImGui::PopFont();
        statusDot(stateColor(st.state));
        ImGui::SameLine(0, s(4));
        mutedText(stateText(st.state));
        ImGui::EndGroup();
        mutedText(std::to_string(st.config.cpus) + " CPU  -  " + formatMemory(st.config.memoryMb) + " RAM  -  " +
                  std::to_string(st.config.diskGb) + " GB disk");
        if (st.state == VmState::Stopped) {
            if (primaryButton("Power on")) {
                selectVm(st.config.name);
                powerOn(st.config.name);
            }
            ImGui::SameLine();
        }
        if (ImGui::Button("Open")) selectVm(st.config.name);
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopID();
    }
}

// ------------------------------------------------------------- full screen

void App::drawFullscreen() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 1));
    ImGui::Begin("##fullscreen", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    const VmStatus* vm = findStatus(selectedVm_);
    auto screen = vm ? screens_.find(vm->config.name) : screens_.end();
    if (!vm || vm->state == VmState::Stopped || screen == screens_.end()) {
        ImGui::End();
        toggleFullscreen();  // the VM went away; drop back to the window
        return;
    }
    screen->second->draw(vp->Size);

    // Toolbar that slides in when the mouse touches the top edge.
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    static bool barVisible = false;
    const float barW = s(560), barH = s(44);
    const ImVec2 barPos(vp->Pos.x + (vp->Size.x - barW) / 2, vp->Pos.y);
    bool overBar = mouse.x >= barPos.x && mouse.x <= barPos.x + barW && mouse.y <= barPos.y + barH;
    barVisible = mouse.y <= vp->Pos.y + s(3) || (barVisible && overBar);
    ImGui::End();

    if (barVisible) {
        ImGui::SetNextWindowPos(barPos);
        ImGui::SetNextWindowSize(ImVec2(barW, barH));
        ImGui::Begin("##fsbar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        ImGui::AlignTextToFramePadding();
        ImGui::PushFont(g_fonts.bold);
        ImGui::TextUnformatted(ellipsize(vmTitle(vm->config), s(200)).c_str());
        ImGui::PopFont();
        ImGui::SameLine();
        if (ImGui::Button("Ctrl+Alt+Del")) screen->second->sendCtrlAltDel();
        ImGui::SameLine();
        if (primaryButton("Exit Full Screen")) toggleFullscreen();
        ImGui::End();
    }
}

// ------------------------------------------------------------- status bar

void App::drawStatusBar() {
    ImGui::Separator();
    statusDot(host_.accelerator == Accelerator::None ? colors::Warning : colors::Success);
    ImGui::SameLine();
    ImGui::TextUnformatted(host_.accelerator == Accelerator::None ? "No hardware acceleration"
                                                                   : ("Hardware acceleration: " + toString(host_.accelerator)).c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", host_.acceleratorDetail.c_str());
    ImGui::SameLine(0, s(24));
    int running = static_cast<int>(std::count_if(statuses_.begin(), statuses_.end(), [](const VmStatus& v) { return v.state != VmState::Stopped; }));
    mutedText(std::to_string(statuses_.size()) + (statuses_.size() == 1 ? " VM  -  " : " VMs  -  ") + std::to_string(running) + " running  -  " +
              std::to_string(host_.totalRamMb / 1024) + " GB RAM, " + std::to_string(host_.logicalCpus) + " CPU threads");

    std::string activity;
    for (auto& label : jobs_.labels()) {
        std::string d = describeJob(label);
        if (!d.empty()) activity = d;
    }
    if (!activity.empty()) {
        ImGui::SameLine(0, s(24));
        spinner(s(6));
        ImGui::SameLine();
        ImGui::TextUnformatted((activity + "...").c_str());
    }

    if (update_ && update_->available) {
        std::string text = "Update available: v" + update_->latestVersion;
        float w = ImGui::CalcTextSize(text.c_str()).x + ImGui::GetStyle().FramePadding.x * 2;
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - w);
        if (ImGui::SmallButton(text.c_str())) openUrl(update_->downloadUrl);
    }
}

// ----------------------------------------------------------------- toasts

void App::drawToasts() {
    double now = ImGui::GetTime();
    toasts_.erase(std::remove_if(toasts_.begin(), toasts_.end(), [&](const Toast& t) { return t.expires < now; }), toasts_.end());

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    float y = vp->WorkPos.y + vp->WorkSize.y - s(44);
    for (size_t i = toasts_.size(); i-- > 0;) {
        Toast& t = toasts_[i];
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - s(16), y), ImGuiCond_Always, ImVec2(1, 1));
        ImGui::SetNextWindowBgAlpha(0.97f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(t.error ? IM_COL32(90, 34, 38, 255) : colors::PanelRaised));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        std::string id = "##toast" + std::to_string(i);
        ImGui::Begin(id.c_str(), nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
        ImGui::PushTextWrapPos(s(380));
        ImGui::TextUnformatted(t.text.c_str());
        ImGui::PopTextWrapPos();
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) t.expires = 0;
        float h = ImGui::GetWindowHeight();
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        y -= h + s(8);
    }
}

// ---------------------------------------------------------------- dialogs

void App::drawConfirmDialog() {
    if (showConfirm_) {
        ImGui::OpenPopup("##confirm");
        showConfirm_ = false;
    }
    ImGui::SetNextWindowSize(ImVec2(s(440), 0));
    centerNextWindow();
    if (ImGui::BeginPopupModal("##confirm", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize)) {
        heading(confirmTitle_);
        ImGui::Spacing();
        wrappedText(confirmText_);
        ImGui::Spacing();
        if (dangerButton(confirmAction_.c_str())) {
            if (confirmCallback_) confirmCallback_();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void App::drawExitDialog() {
    if (showExit_) {
        ImGui::OpenPopup("##exit");
        showExit_ = false;
    }
    ImGui::SetNextWindowSize(ImVec2(s(480), 0));
    centerNextWindow();
    if (ImGui::BeginPopupModal("##exit", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize)) {
        heading("Virtual machines are still running");
        wrappedText("You can keep them running in the background and reconnect when you open EduVM again, "
                    "or power them off now. Powering off is like pulling the plug: unsaved work inside the VM is lost.");
        ImGui::Spacing();
        const bool poweringOff = jobs_.busy("exit-poweroff");
        ImGui::BeginDisabled(poweringOff);
        if (primaryButton("Keep Running")) {
            exitApproved_ = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (dangerButton("Power Off and Exit")) {
            auto b = backend();
            std::vector<std::string> running;
            for (auto& st : statuses_)
                if (st.state != VmState::Stopped) running.push_back(st.config.name);
            jobs_.run("exit-poweroff", [this, b, running]() -> UiCallback {
                for (auto& name : running) {
                    try {
                        b->stop(name, true);
                    } catch (const std::exception&) {
                    }
                }
                return [this] { exitApproved_ = true; };
            });
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndDisabled();
        if (poweringOff) {
            spinner(s(7));
            ImGui::SameLine();
            ImGui::TextUnformatted("Powering off...");
        }
        ImGui::EndPopup();
    }
}

}  // namespace eduvm::app
