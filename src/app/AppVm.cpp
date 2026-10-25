#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>

#include "app/App.hpp"
#include "app/Platform.hpp"
#include "app/Theme.hpp"
#include "core/Strings.hpp"

namespace eduvm::app {

namespace {

const char* osName(GuestOs os) {
    switch (os) {
        case GuestOs::Linux: return "Linux";
        case GuestOs::Windows: return "Windows";
        case GuestOs::Other: return "Other";
    }
    return "";
}

const char* displayName(DisplayMode d) {
    switch (d) {
        case DisplayMode::Embedded: return "Inside EduVM";
        case DisplayMode::Window: return "Separate window";
        case DisplayMode::Headless: return "None (headless)";
    }
    return "";
}

std::string networkSummary(const VmConfig& c) {
    std::vector<std::string> parts;
    if (c.nat) parts.push_back("NAT");
    for (auto& seg : c.segments) parts.push_back("LAN segment '" + seg + "'");
    return parts.empty() ? "Not connected" : join(parts, ", ");
}

}  // namespace

void App::selectVm(const std::string& name) {
    page_ = Page::Vm;
    selectedVm_ = name;
    const VmStatus* st = findStatus(name);
    bool console = st && st->state != VmState::Stopped && st->config.display == DisplayMode::Embedded;
    vmTab_ = console ? VmTab::Console : VmTab::Summary;
    forceTab_ = true;
}

VmScreen* App::screenFor(const VmStatus& vm) {
    auto& screen = screens_[vm.config.name];
    if (!screen) screen = std::make_unique<VmScreen>(vm.config.name);
    if (screen->port() != vm.runtime->vncPort) screen->connect(vm.runtime->vncPort);
    return screen.get();
}

void App::pruneScreens() {
    for (auto it = screens_.begin(); it != screens_.end();) {
        const VmStatus* st = findStatus(it->first);
        bool keep = st && st->state != VmState::Stopped && st->runtime && st->runtime->vncPort == it->second->port();
        it = keep ? std::next(it) : screens_.erase(it);
    }
}

void App::powerOn(const std::string& name) {
    if (jobs_.busy("start:" + name)) return;
    auto b = backend();
    HostInfo host = host_;
    jobs_.run("start:" + name, [this, b, host, name]() -> UiCallback {
        VmConfig cfg = repo_.load(name);
        auto advice = recommendResources(host, cfg.cpus, cfg.memoryMb);
        b->start(cfg);
        return [this, advice] {
            monitor_.refreshSoon();
            for (auto& w : advice.warnings) toast(w);
        };
    });
}

void App::powerOff(const std::string& name, bool force) {
    auto b = backend();
    jobs_.run("stop:" + name, [this, b, name, force]() -> UiCallback {
        b->stop(name, force);
        return [this] { monitor_.refreshSoon(); };
    });
}

void App::setPaused(const std::string& name, bool paused) {
    try {
        if (paused) backend()->pause(name);
        else backend()->resume(name);
        monitor_.refreshSoon();
    } catch (const std::exception& e) {
        toast(e.what(), true);
    }
}

void App::loadSnapshots(const std::string& name) {
    if (jobs_.busy("snaplist:" + name)) return;
    auto b = backend();
    jobs_.run("snaplist:" + name, [this, b, name]() -> UiCallback {
        auto list = b->listSnapshots(name);
        return [this, name, list] { snapshots_[name] = list; };
    });
}

void App::openVmSettings(const VmConfig& config, SettingsPage page) {
    vmEdit_ = config;
    settingsPage_ = page;
    expandTo_ = config.diskGb;
    newSegment_.clear();
    showVmSettings_ = true;
}

void App::deleteVm(const VmConfig& config) {
    std::string name = config.name, title = vmTitle(config);
    confirm("Delete " + title + "?", "The virtual machine and its disk are deleted from this computer. This cannot be undone.",
            "Delete from Disk", [this, name, title] {
                try {
                    repo_.remove(name);
                    snapshots_.erase(name);
                    page_ = Page::Home;
                    monitor_.refreshSoon();
                    toast("Deleted " + title);
                } catch (const std::exception& e) {
                    toast(e.what(), true);
                }
            });
}

// ------------------------------------------------------------------ page

void App::drawVmPage() {
    const VmStatus* found = findStatus(selectedVm_);
    if (!found) {
        if (isValidVmName(selectedVm_) && repo_.exists(selectedVm_)) mutedText("Loading...");
        else page_ = Page::Home;
        return;
    }
    const VmStatus vm = *found;

    // Follow power state changes: show the console when a VM boots, the
    // summary when it powers off.
    auto last = lastStates_.find(vm.config.name);
    if (last != lastStates_.end() && last->second != vm.state) {
        if (last->second == VmState::Stopped && vm.state == VmState::Running && vm.config.display == DisplayMode::Embedded) {
            vmTab_ = VmTab::Console;
            forceTab_ = true;
        } else if (vm.state == VmState::Stopped && vmTab_ == VmTab::Console) {
            vmTab_ = VmTab::Summary;
            forceTab_ = true;
        }
    }
    lastStates_[vm.config.name] = vm.state;

    drawVmHeader(vm);
    ImGui::Spacing();

    if (ImGui::BeginTabBar("##vmtabs")) {
        // Capture the requested tab first: the currently open tab item sets
        // vmTab_ while drawing, which must not cancel the switch.
        const VmTab wanted = vmTab_;
        const bool force = forceTab_;
        auto flags = [&](VmTab t) { return (force && wanted == t) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None; };
        if (vm.config.display == DisplayMode::Embedded && ImGui::BeginTabItem("Console", nullptr, flags(VmTab::Console))) {
            vmTab_ = VmTab::Console;
            drawConsole(vm, ImGui::GetContentRegionAvail());
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Summary", nullptr, flags(VmTab::Summary))) {
            vmTab_ = VmTab::Summary;
            drawSummary(vm);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Snapshots", nullptr, flags(VmTab::Snapshots))) {
            if (vmTab_ != VmTab::Snapshots || !snapshots_.count(vm.config.name)) loadSnapshots(vm.config.name);
            vmTab_ = VmTab::Snapshots;
            drawSnapshots(vm);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    forceTab_ = false;
}

void App::drawVmHeader(const VmStatus& vm) {
    const std::string& name = vm.config.name;
    osBadge(toString(vm.config.guestOs), s(44));
    ImGui::SameLine();
    ImGui::BeginGroup();
    heading(vmTitle(vm.config));
    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + s(4));
    ImU32 chipColor = vm.state == VmState::Running ? colors::Success : vm.state == VmState::Paused ? colors::Warning : colors::Muted;
    chip(vm.state == VmState::Running ? "Running" : vm.state == VmState::Paused ? "Suspended" : vm.state == VmState::Unknown ? "Busy" : "Powered off",
         chipColor);
    ImGui::EndGroup();

    // Toolbar
    const bool busy = jobs_.busy("start:" + name) || jobs_.busy("stop:" + name);
    ImGui::BeginDisabled(busy);
    if (vm.state == VmState::Stopped) {
        if (primaryButton(jobs_.busy("start:" + name) ? "Powering on..." : "Power On")) powerOn(name);
    } else {
        if (vm.state == VmState::Paused) {
            if (primaryButton("Resume")) setPaused(name, false);
        } else {
            if (ImGui::Button("Shut Down")) powerOff(name, false);
            ImGui::SetItemTooltip("Asks the guest operating system to shut down cleanly.");
            ImGui::SameLine();
            if (ImGui::Button("Suspend")) setPaused(name, true);
            ImGui::SetItemTooltip("Freezes the VM; Resume continues exactly where it was.");
        }
        ImGui::SameLine();
        if (ImGui::Button("Power Off"))
            confirm("Power off " + vmTitle(vm.config) + "?", "This cuts the power immediately. Unsaved work inside the VM is lost.",
                    "Power Off", [this, name] { powerOff(name, true); });
        auto screen = screens_.find(name);
        if (screen != screens_.end()) {
            ImGui::SameLine();
            if (ImGui::Button("Ctrl+Alt+Del")) screen->second->sendCtrlAltDel();
            ImGui::SameLine();
            if (ImGui::Button("Full Screen")) toggleFullscreen();
            ImGui::SetItemTooltip("Ctrl+Alt+Enter");
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Settings")) openVmSettings(vm.config);
    ImGui::EndDisabled();
}

void App::drawConsole(const VmStatus& vm, const ImVec2& avail) {
    if (vm.state == VmState::Stopped) {
        ImVec2 c = ImGui::GetCursorPos();
        ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetCursorScreenPos(),
                                                  ImVec2(ImGui::GetCursorScreenPos().x + avail.x, ImGui::GetCursorScreenPos().y + avail.y),
                                                  IM_COL32(0, 0, 0, 255), 6);
        const char* msg = "This virtual machine is powered off.";
        float w = ImGui::CalcTextSize(msg).x;
        ImGui::SetCursorPos(ImVec2(c.x + (avail.x - w) / 2, c.y + avail.y * 0.42f));
        mutedText(msg);
        ImGui::SetCursorPosX(c.x + (avail.x - s(160)) / 2);
        ImGui::BeginDisabled(jobs_.busy("start:" + vm.config.name));
        if (primaryButton("Power On", ImVec2(s(160), s(36)))) powerOn(vm.config.name);
        ImGui::EndDisabled();
        return;
    }
    if (!vm.runtime || vm.runtime->vncPort == 0) {
        wrappedText("This VM was started without an embedded display. Shut it down and power it on again to see it here.");
        return;
    }

    VmScreen* screen = screenFor(vm);
    float footer = ImGui::GetTextLineHeightWithSpacing() + s(4);
    screen->draw(ImVec2(avail.x, std::max(s(100), avail.y - footer)));

    if (screen->state() == RfbClient::State::Disconnected) {
        mutedText("Display connection lost: " + screen->error());
        ImGui::SameLine();
        if (ImGui::SmallButton("Reconnect")) screens_.erase(vm.config.name);
    } else if (screen->hasKeyboard()) {
        mutedText("Keyboard and mouse go to the VM. Press Ctrl+Alt to release. Ctrl+Alt+Enter: full screen.");
    } else {
        mutedText("Click inside the screen to control the VM.");
    }
}

void App::drawSummary(const VmStatus& vm) {
    const VmConfig& c = vm.config;
    const bool stopped = vm.state == VmState::Stopped;
    ImGui::Spacing();

    if (!ImGui::BeginTable("##summary", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_PadOuterX)) return;
    ImGui::TableNextRow();

    // Left column: actions and devices
    ImGui::TableNextColumn();
    if (stopped) {
        if (linkButton("Power on this virtual machine")) powerOn(c.name);
    } else if (vm.state == VmState::Running) {
        if (linkButton("Shut down this virtual machine")) powerOff(c.name, false);
    }
    if (linkButton("Edit virtual machine settings")) openVmSettings(c);
    if (stopped && linkButton("Clone this virtual machine")) {
        cloneSource_ = c.name;
        cloneName_ = vmTitle(c) + " (clone)";
        showClone_ = true;
    }
    if (linkButton("Open the VM folder")) openFolder(repo_.vmDir(c.name));
    if (stopped && linkButton("Delete from disk")) deleteVm(c);

    ImGui::Spacing();
    ImGui::SeparatorText("Devices");
    if (ImGui::BeginTable("##devices", 2, ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("device", ImGuiTableColumnFlags_WidthFixed, s(150));
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
        auto row = [&](const char* device, const std::string& value, SettingsPage page) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(device);
            if (ImGui::Selectable(device, false, ImGuiSelectableFlags_SpanAllColumns)) openVmSettings(c, page);
            ImGui::PopID();
            ImGui::TableNextColumn();
            mutedText(value);
        };
        row("Memory", formatMemory(c.memoryMb), SettingsPage::Memory);
        row("Processors", std::to_string(c.cpus), SettingsPage::Processors);
        row("Hard Disk", std::to_string(c.diskGb) + " GB", SettingsPage::HardDisk);
        row("CD/DVD", c.isoPath.empty() ? "Empty" : pathToUtf8(utf8ToPath(c.isoPath).filename()), SettingsPage::CdDvd);
        row("Network Adapter", networkSummary(c), SettingsPage::Network);
        row("Display", displayName(c.display), SettingsPage::Display);
        ImGui::EndTable();
    }

    // Right column: description and details
    ImGui::TableNextColumn();
    ImGui::SeparatorText("Description");
    if (c.description.empty()) {
        if (linkButton("Click here to add a description")) openVmSettings(c, SettingsPage::Options);
    } else {
        wrappedText(c.description);
    }
    ImGui::Spacing();
    ImGui::SeparatorText("Virtual Machine Details");
    if (ImGui::BeginTable("##details", 2)) {
        ImGui::TableSetupColumn("key", ImGuiTableColumnFlags_WidthFixed, s(130));
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
        auto row = [](const char* k, const std::string& v) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            mutedText(k);
            ImGui::TableNextColumn();
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(v.c_str());
            ImGui::PopTextWrapPos();
        };
        row("State", vm.state == VmState::Stopped ? "Powered off" : vm.state == VmState::Paused ? "Suspended" : "Running");
        row("Guest OS", osName(c.guestOs));
        row("Configuration", pathToUtf8(repo_.vmDir(c.name) / "vm.json"));
        row("Hard disk file", pathToUtf8(repo_.diskPath(c)));
        if (!c.createdAt.empty()) row("Created", c.createdAt.substr(0, 10));
        if (vm.runtime && vm.runtime->sshPort) row("SSH", "ssh -p " + std::to_string(vm.runtime->sshPort) + " <user>@127.0.0.1");
        ImGui::EndTable();
    }
    if (vm.runtime && vm.runtime->sshPort && ImGui::SmallButton("Copy SSH command")) {
        ImGui::SetClipboardText(("ssh -p " + std::to_string(vm.runtime->sshPort) + " user@127.0.0.1").c_str());
        toast("Copied. Replace 'user' with your username inside the VM.");
    }
    ImGui::EndTable();
}

void App::drawSnapshots(const VmStatus& vm) {
    const std::string& name = vm.config.name;
    const bool running = vm.state != VmState::Stopped;
    ImGui::Spacing();
    if (!running) wrappedText("A snapshot saves the virtual machine's disk as it is now, so you can always go back to it.");
    else if (host_.accelerator == Accelerator::Whpx)
        wrappedText("A snapshot saves the VM's disk as it is right now. To restore one, EduVM powers the VM off first. "
                    "(Saving memory as well is not supported by Windows Hypervisor Platform.)");
    else wrappedText("The VM is running, so snapshots also save its memory: restoring brings you back to this exact moment.");
    ImGui::Spacing();

    ImGui::SetNextItemWidth(s(280));
    ImGui::InputTextWithHint("##snapname", "Snapshot name, e.g. before-update", &snapshotName_);
    ImGui::SameLine();
    const bool busy = jobs_.busy("snap:" + name);
    ImGui::BeginDisabled(busy || !isValidVmName(snapshotName_));
    if (primaryButton("Take Snapshot")) {
        auto b = backend();
        std::string snap = snapshotName_;
        jobs_.run("snap:" + name, [this, b, name, snap]() -> UiCallback {
            b->createSnapshot(name, snap);
            return [this, name, snap] {
                snapshotName_.clear();
                loadSnapshots(name);
                toast("Snapshot '" + snap + "' saved");
            };
        });
    }
    ImGui::EndDisabled();
    if (!snapshotName_.empty() && !isValidVmName(snapshotName_)) mutedText("Use letters, digits, - and _ only.");

    ImGui::Spacing();
    auto it = snapshots_.find(name);
    if (it == snapshots_.end()) {
        spinner(s(7));
        return;
    }
    if (it->second.empty()) {
        mutedText("No snapshots yet.");
        return;
    }
    if (ImGui::BeginTable("##snaps", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Taken");
        ImGui::TableSetupColumn("Type");
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, s(190));
        ImGui::TableHeadersRow();
        for (auto& snap : it->second) {
            ImGui::PushID(snap.name.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(snap.name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(snap.date.c_str());
            ImGui::TableNextColumn();
            mutedText(snap.includesRam ? "Disk and memory" : "Disk");
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(busy);
            std::string snapName = snap.name;
            if (ImGui::SmallButton("Restore")) {
                const bool powerOffFirst = running && !snap.includesRam;  // a disk-only snapshot can't load into a running VM
                confirm("Restore '" + snapName + "'?",
                        powerOffFirst ? "The VM will be powered off first. Everything that changed since this snapshot is lost."
                                      : "Everything that changed since this snapshot is lost.",
                        powerOffFirst ? "Power Off and Restore" : "Restore", [this, name, snapName, powerOffFirst] {
                            auto b = backend();
                            jobs_.run("snap:" + name, [this, b, name, snapName, powerOffFirst]() -> UiCallback {
                                if (powerOffFirst) b->stop(name, true);
                                b->restoreSnapshot(name, snapName);
                                return [this, snapName, powerOffFirst] {
                                    monitor_.refreshSoon();
                                    toast("Restored '" + snapName + "'" + (powerOffFirst ? ". Press Power On to start it." : ""));
                                };
                            });
                        });
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Delete")) {
                confirm("Delete snapshot '" + snapName + "'?", "The snapshot is removed. The virtual machine itself is not changed.",
                        "Delete", [this, name, snapName] {
                            auto b = backend();
                            jobs_.run("snap:" + name, [this, b, name, snapName]() -> UiCallback {
                                b->deleteSnapshot(name, snapName);
                                return [this, name] { loadSnapshots(name); };
                            });
                        });
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

}  // namespace eduvm::app
