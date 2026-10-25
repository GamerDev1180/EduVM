#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>

#include "app/App.hpp"
#include "app/Platform.hpp"
#include "app/Theme.hpp"
#include "core/Paths.hpp"
#include "core/Strings.hpp"
#include "eduvm/Version.hpp"

namespace fs = std::filesystem;

namespace eduvm::app {

namespace {

const char* kOsNames[] = {"Linux", "Windows", "Other"};
const GuestOs kOsValues[] = {GuestOs::Linux, GuestOs::Windows, GuestOs::Other};
const char* kIsoFilter = "Disc images (*.iso)|*.iso|All files|*.*";
const char* kDiskFilter =
    "Virtual disks (*.vmdk, *.vdi, *.vhd, *.vhdx, *.qcow2, *.img)|*.vmdk;*.vdi;*.vhd;*.vhdx;*.qcow2;*.img;*.raw|All files|*.*";

int osIndex(GuestOs os) {
    for (int i = 0; i < 3; ++i)
        if (kOsValues[i] == os) return i;
    return 2;
}

int memoryCap(const HostInfo& host) { return std::max(1024, static_cast<int>(host.totalRamMb * 6 / 10) / 256 * 256); }

// Memory slider snapped to 256 MB steps, showing "2 GB" instead of "2048".
bool memorySlider(const char* label, int* mb, const HostInfo& host) {
    std::string text = formatMemory(*mb);
    bool changed = ImGui::SliderInt(label, mb, 512, memoryCap(host), text.c_str());
    if (changed) *mb = std::max(512, (*mb + 128) / 256 * 256);
    return changed;
}

void warnings(const HostInfo& host, int cpus, int memoryMb) {
    for (auto& w : recommendResources(host, cpus, memoryMb).warnings) {
        if (startsWith(w, "No hardware acceleration")) continue;  // shown in the status bar already
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colors::Warning));
        wrappedText(w);
        ImGui::PopStyleColor();
    }
}

bool footerButtons(bool& cancel, const char* primary, bool primaryEnabled, const char* secondary = nullptr,
                   bool* secondaryPressed = nullptr) {
    ImGui::Separator();
    float w = ImGui::CalcTextSize(primary).x + ImGui::CalcTextSize("Cancel").x + ImGui::GetStyle().FramePadding.x * 4 +
              ImGui::GetStyle().ItemSpacing.x + 40;
    if (secondary) w += ImGui::CalcTextSize(secondary).x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemSpacing.x + 20;
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - w));
    if (secondary) {
        if (ImGui::Button(secondary, ImVec2(ImGui::CalcTextSize(secondary).x + 20, 0)) && secondaryPressed) *secondaryPressed = true;
        ImGui::SameLine();
    }
    ImGui::BeginDisabled(!primaryEnabled);
    bool ok = primaryButton(primary, ImVec2(ImGui::CalcTextSize(primary).x + 40, 0));
    ImGui::EndDisabled();
    ImGui::SameLine();
    cancel = ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape);
    return ok;
}

}  // namespace

// ------------------------------------------------------------ create a VM

void App::createVm(VmConfig cfg, std::optional<std::string> importFrom, bool powerOnAfter) {
    auto b = backend();
    jobs_.run("newvm:" + vmTitle(cfg), [this, b, cfg, importFrom, powerOnAfter]() mutable -> UiCallback {
        repo_.save(cfg);
        try {
            if (importFrom) {
                b->convertToQcow2(utf8ToPath(*importFrom), repo_.diskPath(cfg));
                cfg.diskGb = b->diskVirtualSizeGb(repo_.diskPath(cfg));
                repo_.save(cfg);
            } else {
                b->createDisk(repo_.diskPath(cfg), cfg.diskGb);
            }
        } catch (...) {
            repo_.remove(cfg.name);
            throw;
        }
        return [this, name = cfg.name, title = vmTitle(cfg), powerOnAfter] {
            monitor_.refreshSoon();
            selectVm(name);
            toast("Created " + title);
            if (powerOnAfter) powerOn(name);
        };
    });
}

void App::drawNewVmWizard() {
    if (showWizard_) {
        ImGui::OpenPopup("New Virtual Machine Wizard");
        showWizard_ = false;
        wizardStep_ = 0;
        wizardSource_ = 0;
        wizardIso_.clear();
        wizardGuess_ = defaultsFor(GuestOs::Linux);
        wizardOs_ = 0;
        wizardName_.clear();
        wizardDisk_ = wizardGuess_.diskGb;
        wizardMemory_ = wizardGuess_.memoryMb;
        wizardCpus_ = std::min(2, static_cast<int>(host_.logicalCpus));
        wizardPowerOn_ = true;
    }
    ImGui::SetNextWindowSize(ImVec2(s(640), s(500)), ImGuiCond_Appearing);
    centerNextWindow();
    if (!ImGui::BeginPopupModal("New Virtual Machine Wizard", nullptr, ImGuiWindowFlags_NoResize)) return;

    auto applyDefaults = [&](const OsGuess& g, bool setName) {
        wizardGuess_ = g;
        wizardOs_ = osIndex(g.os);
        wizardMemory_ = std::min(g.memoryMb, memoryCap(host_));
        wizardDisk_ = g.diskGb;
        if (setName) wizardName_ = g.name;
    };

    // Step indicator
    static const char* steps[] = {"Installation", "Operating system", "Name", "Disk", "Finish"};
    for (int i = 0; i < 5; ++i) {
        if (i) {
            ImGui::SameLine(0, s(6));
            mutedText(">");
            ImGui::SameLine(0, s(6));
        }
        if (i == wizardStep_) {
            ImGui::PushFont(g_fonts.bold);
            ImGui::TextUnformatted(steps[i]);
            ImGui::PopFont();
        } else {
            mutedText(steps[i]);
        }
    }
    ImGui::Separator();
    ImGui::Spacing();

    const float bodyH = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() - s(12);
    ImGui::BeginChild("##wizardbody", ImVec2(0, bodyH));
    switch (wizardStep_) {
        case 0: {
            heading("How will you install the operating system?");
            ImGui::Spacing();
            ImGui::RadioButton("Installer disc image file (ISO)", &wizardSource_, 0);
            ImGui::Indent(s(28));
            ImGui::BeginDisabled(wizardSource_ != 0);
            ImGui::SetNextItemWidth(s(420));
            if (ImGui::InputTextWithHint("##iso", "Path to an .iso file", &wizardIso_)) {
                OsGuess g = guessOsFromIso(wizardIso_);
                if (g.detected) applyDefaults(g, true);
            }
            ImGui::SameLine();
            if (ImGui::Button("Browse...")) {
                if (auto iso = openFileDialog("Choose installer disc image", kIsoFilter)) {
                    wizardIso_ = *iso;
                    OsGuess g = guessOsFromIso(wizardIso_);
                    applyDefaults(g.detected ? g : defaultsFor(kOsValues[wizardOs_]), g.detected);
                }
            }
            if (!wizardIso_.empty()) {
                bool exists = fs::exists(utf8ToPath(wizardIso_));
                if (!exists) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colors::Danger));
                    ImGui::TextUnformatted("File not found.");
                    ImGui::PopStyleColor();
                } else if (wizardGuess_.detected) {
                    statusIcon(true);
                    ImGui::SameLine();
                    ImGui::TextUnformatted(("Detected " + wizardGuess_.name + ".").c_str());
                } else {
                    mutedText("Could not detect which operating system this is; you can choose it in the next step.");
                }
            }
            ImGui::EndDisabled();
            ImGui::Unindent(s(28));
            ImGui::Spacing();
            ImGui::RadioButton("I will install the operating system later", &wizardSource_, 1);
            ImGui::Indent(s(28));
            mutedText("The virtual machine is created with a blank hard disk. You can insert a disc later in its settings.");
            ImGui::Unindent(s(28));
            break;
        }
        case 1: {
            heading("Which operating system will this VM run?");
            ImGui::Spacing();
            for (int i = 0; i < 3; ++i) {
                if (ImGui::RadioButton(kOsNames[i], &wizardOs_, i)) {
                    bool nameWasDefault = wizardName_.empty() || wizardName_ == wizardGuess_.name;
                    OsGuess g = wizardGuess_.detected && wizardGuess_.os == kOsValues[i] ? wizardGuess_ : defaultsFor(kOsValues[i]);
                    applyDefaults(g, nameWasDefault);
                }
            }
            ImGui::Spacing();
            mutedText(kOsValues[wizardOs_] == GuestOs::Windows
                          ? "Windows VMs get hardware that Windows supports out of the box (SATA disk, Intel network card)."
                          : "Linux VMs get fast paravirtualised (virtio) disk and network devices.");
            break;
        }
        case 2: {
            heading("Name the virtual machine");
            ImGui::Spacing();
            if (wizardName_.empty()) wizardName_ = wizardGuess_.name;
            ImGui::SetNextItemWidth(s(420));
            ImGui::InputText("Virtual machine name", &wizardName_);
            ImGui::Spacing();
            mutedText("Location:");
            wrappedText(pathToUtf8(vmsDir() / uniqueVmName(wizardName_)));
            break;
        }
        case 3: {
            heading("Specify disk capacity");
            ImGui::Spacing();
            ImGui::SetNextItemWidth(s(420));
            ImGui::SliderInt("Maximum disk size", &wizardDisk_, 8, 512, "%d GB");
            ImGui::Spacing();
            wrappedText("The disk file starts small and grows as the virtual machine stores data, up to this maximum. "
                        "You can expand it later.");
            mutedText(std::to_string(host_.freeDiskMb / 1024) + " GB free on this computer.");
            break;
        }
        case 4: {
            heading("Ready to create the virtual machine");
            ImGui::Spacing();
            if (ImGui::BeginTable("##ready", 2, ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("key", ImGuiTableColumnFlags_WidthFixed, s(150));
                ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
                auto row = [](const char* k, const std::string& v) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    mutedText(k);
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(v.c_str());
                };
                row("Name", wizardName_);
                row("Operating system", kOsNames[wizardOs_]);
                row("Hard disk", std::to_string(wizardDisk_) + " GB, grows as needed");
                row("CD/DVD", wizardSource_ == 0 ? pathToUtf8(utf8ToPath(wizardIso_).filename()) : "Empty");
                row("Network", "NAT (shares this computer's internet connection)");
                ImGui::EndTable();
            }
            ImGui::Spacing();
            ImGui::SeparatorText("Hardware");
            ImGui::SetNextItemWidth(s(360));
            memorySlider("Memory", &wizardMemory_, host_);
            ImGui::SetNextItemWidth(s(360));
            ImGui::SliderInt("Processors", &wizardCpus_, 1, static_cast<int>(host_.logicalCpus));
            warnings(host_, wizardCpus_, wizardMemory_);
            ImGui::Spacing();
            ImGui::Checkbox("Power on this virtual machine after creation", &wizardPowerOn_);
            break;
        }
    }
    ImGui::EndChild();

    bool canNext = true;
    if (wizardStep_ == 0) canNext = wizardSource_ == 1 || (!wizardIso_.empty() && fs::exists(utf8ToPath(wizardIso_)));
    if (wizardStep_ == 2) canNext = !trim(wizardName_).empty();

    bool cancel = false, back = false;
    const bool last = wizardStep_ == 4;
    bool next = footerButtons(cancel, last ? "Finish" : "Next", canNext, wizardStep_ > 0 ? "Back" : nullptr, &back);
    if (back) --wizardStep_;
    if (next && !last) ++wizardStep_;
    if (next && last) {
        VmConfig cfg;
        cfg.displayName = trim(wizardName_);
        cfg.name = uniqueVmName(cfg.displayName);
        cfg.guestOs = kOsValues[wizardOs_];
        cfg.cpus = wizardCpus_;
        cfg.memoryMb = wizardMemory_;
        cfg.diskGb = wizardDisk_;
        if (wizardSource_ == 0) cfg.isoPath = pathToUtf8(fs::absolute(utf8ToPath(wizardIso_)));
        cfg.createdAt = nowIso8601();
        createVm(cfg, std::nullopt, wizardPowerOn_);
        ImGui::CloseCurrentPopup();
    }
    if (cancel) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// --------------------------------------------------- virtual machine settings

void App::drawVmSettingsDialog() {
    if (showVmSettings_) {
        ImGui::OpenPopup("Virtual Machine Settings");
        showVmSettings_ = false;
    }
    ImGui::SetNextWindowSize(ImVec2(s(800), s(520)), ImGuiCond_Appearing);
    centerNextWindow();
    if (!ImGui::BeginPopupModal("Virtual Machine Settings", nullptr)) return;

    const VmStatus* st = findStatus(vmEdit_.name);
    const bool stopped = !st || st->state == VmState::Stopped;
    VmConfig& e = vmEdit_;

    heading(vmTitle(e));
    if (!stopped) mutedText("The VM is running. Hardware can only be changed while it is powered off.");
    ImGui::Spacing();

    const float bodyH = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() - s(12);
    struct Item {
        SettingsPage page;
        const char* name;
        std::string summary;
    };
    std::string net = e.nat ? "NAT" : "";
    if (!e.segments.empty()) net += std::string(net.empty() ? "" : " + ") + std::to_string(e.segments.size()) + " LAN segment(s)";
    const Item items[] = {
        {SettingsPage::Memory, "Memory", formatMemory(e.memoryMb)},
        {SettingsPage::Processors, "Processors", std::to_string(e.cpus)},
        {SettingsPage::HardDisk, "Hard Disk", std::to_string(e.diskGb) + " GB"},
        {SettingsPage::CdDvd, "CD/DVD", e.isoPath.empty() ? "Empty" : "Using ISO image"},
        {SettingsPage::Network, "Network Adapter", net.empty() ? "Not connected" : net},
        {SettingsPage::Display, "Display", e.display == DisplayMode::Embedded ? "Inside EduVM" : e.display == DisplayMode::Window ? "Separate window" : "None"},
        {SettingsPage::Options, "Options", "Name, OS, notes"},
    };

    ImGui::BeginChild("##devices", ImVec2(s(260), bodyH), ImGuiChildFlags_Borders);
    if (ImGui::BeginTable("##devtable", 2)) {
        ImGui::TableSetupColumn("device", ImGuiTableColumnFlags_WidthFixed, s(130));
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
        for (auto& it : items) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Selectable(it.name, settingsPage_ == it.page, ImGuiSelectableFlags_SpanAllColumns)) settingsPage_ = it.page;
            ImGui::TableNextColumn();
            mutedText(it.summary);
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("##editor", ImVec2(0, bodyH), ImGuiChildFlags_AlwaysUseWindowPadding);
    const bool hardware = settingsPage_ != SettingsPage::Options;
    ImGui::BeginDisabled(hardware && !stopped);
    ImGui::PushItemWidth(s(320));
    switch (settingsPage_) {
        case SettingsPage::Memory:
            ImGui::PushFont(g_fonts.bold);
            ImGui::TextUnformatted("Memory");
            ImGui::PopFont();
            mutedText("How much of this computer's memory the VM may use.");
            ImGui::Spacing();
            memorySlider("Memory for this VM", &e.memoryMb, host_);
            mutedText("This computer has " + formatMemory(static_cast<int>(host_.totalRamMb)) + ". Recommended maximum: " +
                      formatMemory(memoryCap(host_)) + ".");
            warnings(host_, e.cpus, e.memoryMb);
            break;
        case SettingsPage::Processors:
            ImGui::PushFont(g_fonts.bold);
            ImGui::TextUnformatted("Processors");
            ImGui::PopFont();
            ImGui::Spacing();
            ImGui::SliderInt("Number of processor cores", &e.cpus, 1, static_cast<int>(host_.logicalCpus));
            mutedText("This computer has " + std::to_string(host_.logicalCpus) + " processor threads.");
            warnings(host_, e.cpus, e.memoryMb);
            break;
        case SettingsPage::HardDisk: {
            ImGui::PushFont(g_fonts.bold);
            ImGui::TextUnformatted("Hard Disk");
            ImGui::PopFont();
            ImGui::Spacing();
            mutedText("Disk file:");
            wrappedText(pathToUtf8(repo_.diskPath(e)));
            ImGui::Text("Maximum size: %d GB", e.diskGb);
            ImGui::Spacing();
            ImGui::SeparatorText("Expand");
            if (expandTo_ <= e.diskGb) expandTo_ = e.diskGb + 10;
            ImGui::SliderInt("New maximum size", &expandTo_, e.diskGb + 1, std::max(e.diskGb * 4, 512), "%d GB");
            bool busy = jobs_.busy("expand:" + vmTitle(e));
            ImGui::BeginDisabled(busy);
            if (ImGui::Button(busy ? "Expanding..." : "Expand disk")) {
                auto b = backend();
                VmConfig cfg = repo_.load(e.name);
                int size = expandTo_;
                jobs_.run("expand:" + vmTitle(e), [this, b, cfg, size]() mutable -> UiCallback {
                    b->expandDisk(cfg, size);
                    cfg.diskGb = size;
                    repo_.save(cfg);
                    return [this, size, name = cfg.name] {
                        if (vmEdit_.name == name) vmEdit_.diskGb = size;
                        monitor_.refreshSoon();
                        toast("Disk expanded to " + std::to_string(size) + " GB. Extend the partition inside the guest OS to use the space.");
                    };
                });
            }
            ImGui::EndDisabled();
            mutedText("The extra space appears as unallocated space; extend the partition inside the guest to use it.");
            break;
        }
        case SettingsPage::CdDvd: {
            ImGui::PushFont(g_fonts.bold);
            ImGui::TextUnformatted("CD/DVD Drive");
            ImGui::PopFont();
            ImGui::Spacing();
            int mode = e.isoPath.empty() ? 0 : 1;
            if (ImGui::RadioButton("No disc", &mode, 0)) e.isoPath.clear();
            ImGui::RadioButton("Use ISO image file:", &mode, 1);
            if (mode == 1) {
                ImGui::Indent(s(28));
                ImGui::InputTextWithHint("##iso", "Path to an .iso file", &e.isoPath);
                ImGui::SameLine();
                if (ImGui::Button("Browse...")) {
                    if (auto iso = openFileDialog("Choose disc image", kIsoFilter)) e.isoPath = *iso;
                }
                if (!e.isoPath.empty() && !fs::exists(utf8ToPath(e.isoPath))) mutedText("File not found.");
                mutedText("The VM boots from the disc first when one is inserted. Choose 'No disc' after installing.");
                ImGui::Unindent(s(28));
            }
            break;
        }
        case SettingsPage::Network: {
            ImGui::PushFont(g_fonts.bold);
            ImGui::TextUnformatted("Network Adapter");
            ImGui::PopFont();
            ImGui::Spacing();
            ImGui::Checkbox("NAT: share this computer's internet connection", &e.nat);
            ImGui::Indent(s(28));
            ImGui::BeginDisabled(!e.nat);
            ImGui::Checkbox("Forward SSH (port 22) to a local port", &e.forwardSsh);
            ImGui::EndDisabled();
            ImGui::Unindent(s(28));
            ImGui::Spacing();
            ImGui::SeparatorText("LAN segments");
            wrappedText("A LAN segment is a private network between VMs. VMs connected to the same segment name can reach "
                        "each other, but nothing else.");
            for (size_t i = 0; i < e.segments.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                ImGui::BulletText("%s", e.segments[i].c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) {
                    e.segments.erase(e.segments.begin() + static_cast<long>(i));
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            ImGui::SetNextItemWidth(s(220));
            ImGui::InputTextWithHint("##seg", "Segment name, e.g. lab-net", &newSegment_);
            ImGui::SameLine();
            ImGui::BeginDisabled(trim(newSegment_).empty() ||
                                 std::find(e.segments.begin(), e.segments.end(), trim(newSegment_)) != e.segments.end());
            if (ImGui::Button("Add")) {
                e.segments.push_back(trim(newSegment_));
                newSegment_.clear();
            }
            ImGui::EndDisabled();
            break;
        }
        case SettingsPage::Display: {
            ImGui::PushFont(g_fonts.bold);
            ImGui::TextUnformatted("Display");
            ImGui::PopFont();
            ImGui::Spacing();
            int d = e.display == DisplayMode::Embedded ? 0 : e.display == DisplayMode::Window ? 1 : 2;
            ImGui::RadioButton("Inside EduVM (recommended)", &d, 0);
            ImGui::RadioButton("In a separate QEMU window", &d, 1);
            ImGui::RadioButton("No display (headless, use SSH)", &d, 2);
            e.display = d == 0 ? DisplayMode::Embedded : d == 1 ? DisplayMode::Window : DisplayMode::Headless;
            break;
        }
        case SettingsPage::Options: {
            ImGui::PushFont(g_fonts.bold);
            ImGui::TextUnformatted("Options");
            ImGui::PopFont();
            ImGui::Spacing();
            ImGui::InputText("Name", &e.displayName);
            int os = osIndex(e.guestOs);
            ImGui::BeginDisabled(!stopped);
            if (ImGui::Combo("Guest OS", &os, kOsNames, 3)) e.guestOs = kOsValues[os];
            ImGui::EndDisabled();
            ImGui::Spacing();
            mutedText("Notes");
            ImGui::InputTextMultiline("##desc", &e.description, ImVec2(-1, s(140)));
            break;
        }
    }
    ImGui::PopItemWidth();
    ImGui::EndDisabled();
    ImGui::EndChild();

    bool cancel = false;
    if (footerButtons(cancel, "OK", !trim(e.displayName).empty() || !e.name.empty())) {
        try {
            if (trim(e.displayName).empty()) e.displayName = e.name;
            VmConfig current = repo_.load(e.name);
            e.diskGb = current.diskGb;  // the disk size is owned by Expand, not by this form
            repo_.save(e);
            monitor_.refreshSoon();
        } catch (const std::exception& ex) {
            toast(ex.what(), true);
        }
        ImGui::CloseCurrentPopup();
    }
    if (cancel) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// ------------------------------------------------------------ import/clone

void App::drawImportDialog() {
    if (showImport_) {
        ImGui::OpenPopup("Import Disk Image");
        showImport_ = false;
        importPath_.clear();
        importName_.clear();
        importOs_ = 0;
        importMemory_ = 2048;
        importCpus_ = std::min(2, static_cast<int>(host_.logicalCpus));
    }
    ImGui::SetNextWindowSize(ImVec2(s(600), 0));
    centerNextWindow();
    if (!ImGui::BeginPopupModal("Import Disk Image", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    wrappedText("Create a virtual machine from an existing virtual disk, for example one exported from VMware (.vmdk), "
                "VirtualBox (.vdi) or Hyper-V (.vhdx).");
    ImGui::Spacing();
    ImGui::SetNextItemWidth(s(420));
    ImGui::InputTextWithHint("##disk", "Path to a disk image", &importPath_);
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
        if (auto file = openFileDialog("Choose a virtual disk", kDiskFilter)) {
            importPath_ = *file;
            if (importName_.empty()) importName_ = pathToUtf8(utf8ToPath(*file).stem());
            OsGuess g = guessOsFromIso(*file);
            if (g.detected) {
                importOs_ = osIndex(g.os);
                importMemory_ = std::min(g.memoryMb, memoryCap(host_));
            }
        }
    }
    ImGui::PushItemWidth(s(360));
    ImGui::InputText("Name", &importName_);
    ImGui::Combo("Guest OS", &importOs_, kOsNames, 3);
    memorySlider("Memory", &importMemory_, host_);
    ImGui::SliderInt("Processors", &importCpus_, 1, static_cast<int>(host_.logicalCpus));
    ImGui::PopItemWidth();
    mutedText("The disk is copied into EduVM's format; the original file is not changed.");
    ImGui::Spacing();

    bool cancel = false;
    bool valid = !importPath_.empty() && fs::exists(utf8ToPath(importPath_)) && !trim(importName_).empty();
    if (footerButtons(cancel, "Import", valid)) {
        VmConfig cfg;
        cfg.displayName = trim(importName_);
        cfg.name = uniqueVmName(cfg.displayName);
        cfg.guestOs = kOsValues[importOs_];
        cfg.memoryMb = importMemory_;
        cfg.cpus = importCpus_;
        cfg.createdAt = nowIso8601();
        createVm(cfg, importPath_, false);
        ImGui::CloseCurrentPopup();
    }
    if (cancel) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void App::drawCloneDialog() {
    if (showClone_) {
        ImGui::OpenPopup("Clone Virtual Machine");
        showClone_ = false;
    }
    centerNextWindow();
    if (!ImGui::BeginPopupModal("Clone Virtual Machine", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    wrappedText("A clone is a full, independent copy of the virtual machine and its disk.");
    ImGui::SetNextItemWidth(s(360));
    ImGui::InputText("Name of the clone", &cloneName_);
    ImGui::Spacing();
    bool cancel = false;
    if (footerButtons(cancel, "Clone", !trim(cloneName_).empty())) {
        auto b = backend();
        std::string source = cloneSource_, title = trim(cloneName_), name = uniqueVmName(title);
        jobs_.run("clone:" + title, [this, b, source, title, name]() -> UiCallback {
            VmConfig src = repo_.load(source);
            VmConfig cfg = src;
            cfg.name = name;
            cfg.displayName = title;
            cfg.diskFile = "disk.qcow2";
            cfg.createdAt = nowIso8601();
            repo_.save(cfg);
            try {
                b->convertToQcow2(repo_.diskPath(src), repo_.diskPath(cfg));
            } catch (...) {
                repo_.remove(cfg.name);
                throw;
            }
            return [this, name, title] {
                monitor_.refreshSoon();
                selectVm(name);
                toast("Created clone " + title);
            };
        });
        ImGui::CloseCurrentPopup();
    }
    if (cancel) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// -------------------------------------------------------- app-level dialogs

void App::runDiagnosticsAsync(bool showIfProblems) {
    if (jobs_.busy("diag")) return;
    auto b = backend();
    Settings settings = settings_;
    HostInfo host = host_;
    jobs_.run("diag", [this, b, settings, host, showIfProblems]() -> UiCallback {
        auto items = runDiagnostics(settings, *b, host);
        return [this, items, showIfProblems] {
            diagnostics_ = items;
            if (showIfProblems) {
                for (auto& d : items)
                    if (!d.ok && !d.optional) showDiagnostics_ = true;
            }
        };
    });
}

void App::checkForUpdatesAsync(bool userInitiated) {
    Settings settings = settings_;
    jobs_.run("update", [this, settings, userInitiated]() -> UiCallback {
        try {
            UpdateInfo info = checkForAppUpdate(settings);
            return [this, info, userInitiated] {
                update_ = info;
                if (userInitiated && !info.available) toast("EduVM " + info.currentVersion + " is up to date.");
                if (info.available) toast("EduVM " + info.latestVersion + " is available. Click the button in the status bar.");
            };
        } catch (const std::exception& e) {
            if (!userInitiated) return {};  // background checks fail quietly when offline
            return [this, msg = std::string(e.what())] { toast(msg, true); };
        }
    });
}

void App::drawPreferencesDialog() {
    if (showPreferences_) {
        ImGui::OpenPopup("Preferences");
        showPreferences_ = false;
    }
    ImGui::SetNextWindowSize(ImVec2(s(560), 0));
    centerNextWindow();
    if (!ImGui::BeginPopupModal("Preferences", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    Settings& e = prefsEdit_;

    ImGui::SeparatorText("QEMU");
    ImGui::SetNextItemWidth(s(380));
    ImGui::InputTextWithHint("##qemu", "Detected automatically", &e.qemuDir);
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
        if (auto exe = openFileDialog("Locate qemu-system-x86_64", "QEMU (qemu-system-x86_64*)|qemu-system-x86_64*|All files|*.*"))
            e.qemuDir = pathToUtf8(utf8ToPath(*exe).parent_path());
    }
    mutedText("Leave empty unless QEMU is installed in an unusual place.");

    ImGui::SeparatorText("Updates");
    ImGui::Checkbox("Check for EduVM updates when starting", &e.checkForUpdates);

    ImGui::SeparatorText("Storage");
    ImGui::TextUnformatted("Virtual machines are stored in:");
    if (linkButton(pathToUtf8(vmsDir()))) openFolder(vmsDir());
    ImGui::Spacing();

    bool cancel = false;
    if (footerButtons(cancel, "Save", true)) {
        try {
            settings_ = prefsEdit_;
            settings_.save();
            rebuildBackend();
            runDiagnosticsAsync();
        } catch (const std::exception& ex) {
            toast(ex.what(), true);
        }
        ImGui::CloseCurrentPopup();
    }
    if (cancel) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void App::drawDiagnosticsDialog() {
    if (showDiagnostics_) {
        ImGui::OpenPopup("System Check");
        showDiagnostics_ = false;
    }
    ImGui::SetNextWindowSize(ImVec2(s(640), 0));
    centerNextWindow();
    if (!ImGui::BeginPopupModal("System Check", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    wrappedText("EduVM checks whether this computer is ready to run virtual machines.");
    ImGui::Spacing();
    if (diagnostics_.empty() || jobs_.busy("diag")) {
        spinner(s(7));
        ImGui::SameLine();
        ImGui::TextUnformatted("Checking...");
    }
    for (auto& d : diagnostics_) {
        statusIcon(d.ok);
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::PushFont(g_fonts.bold);
        ImGui::TextUnformatted((d.name + (d.ok || !d.optional ? "" : "  (recommended)")).c_str());
        ImGui::PopFont();
        ImGui::PushTextWrapPos(s(580));
        mutedText(d.detail);
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        ImGui::Spacing();
    }
    if (ImGui::Button("Check Again")) runDiagnosticsAsync();
    ImGui::SameLine();
    if (ImGui::Button("QEMU Download Page")) openUrl("https://www.qemu.org/download/");
    ImGui::SameLine();
    if (primaryButton("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void App::drawAboutDialog() {
    if (showAbout_) {
        ImGui::OpenPopup("About EduVM");
        showAbout_ = false;
    }
    centerNextWindow();
    if (!ImGui::BeginPopupModal("About EduVM", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    heading(std::string("EduVM ") + kVersion);
    wrappedText("Create and run virtual machines.");
    mutedText("Virtual machines run on QEMU with hardware acceleration (" + toString(host_.accelerator) + ").");
    ImGui::Spacing();
    if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

}  // namespace eduvm::app
