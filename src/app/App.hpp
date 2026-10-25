#pragma once

#include <imgui.h>

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "app/Jobs.hpp"
#include "app/StatusMonitor.hpp"
#include "app/VmScreen.hpp"
#include "core/Diagnostics.hpp"
#include "core/HostInfo.hpp"
#include "core/OsDetect.hpp"
#include "core/QemuBackend.hpp"
#include "core/Settings.hpp"
#include "core/Updater.hpp"
#include "core/VmRepository.hpp"

struct GLFWwindow;

namespace eduvm::app {

class App {
public:
    App(float uiScale, GLFWwindow* window);
    ~App();

    void frame();
    // Asked when the window is closed. Returns true if the app may exit now.
    bool requestExit();
    bool exitApproved() const { return exitApproved_; }
    // True while something animates (VM screen, spinner); otherwise the main
    // loop sleeps between events to save battery.
    bool wantsContinuousFrames() const;

private:
    enum class Page { Home, Vm };
    enum class VmTab { Console, Summary, Snapshots };
    enum class SettingsPage { Memory, Processors, HardDisk, CdDvd, Network, Display, Options };

    // --- shell (App.cpp)
    std::shared_ptr<const QemuBackend> backend() const;
    void rebuildBackend();
    void toast(std::string text, bool error = false);
    void confirm(std::string title, std::string text, std::string action, std::function<void()> onConfirm);
    float s(float v) const { return v * scale_; }
    const VmStatus* findStatus(const std::string& name) const;
    std::string uniqueVmName(const std::string& displayName) const;
    void toggleFullscreen();

    void drawMenuBar();
    void drawSidebar();
    void drawHome();
    void drawStatusBar();
    void drawToasts();
    void drawFullscreen();
    void drawConfirmDialog();
    void drawExitDialog();

    // --- virtual machine page (AppVm.cpp)
    void selectVm(const std::string& name);
    void drawVmPage();
    void drawVmHeader(const VmStatus& vm);
    void drawConsole(const VmStatus& vm, const ImVec2& size);
    void drawSummary(const VmStatus& vm);
    void drawSnapshots(const VmStatus& vm);
    void powerOn(const std::string& name);
    void powerOff(const std::string& name, bool force);
    void setPaused(const std::string& name, bool paused);
    void loadSnapshots(const std::string& name);
    void pruneScreens();
    VmScreen* screenFor(const VmStatus& vm);
    void openVmSettings(const VmConfig& config, SettingsPage page = SettingsPage::Memory);
    void deleteVm(const VmConfig& config);

    // --- dialogs (AppDialogs.cpp)
    void drawNewVmWizard();
    void drawVmSettingsDialog();
    void drawImportDialog();
    void drawCloneDialog();
    void drawPreferencesDialog();
    void drawDiagnosticsDialog();
    void drawAboutDialog();
    void runDiagnosticsAsync(bool showIfProblems = false);
    void checkForUpdatesAsync(bool userInitiated);
    void createVm(VmConfig cfg, std::optional<std::string> importFrom, bool powerOnAfter);

    float scale_;
    GLFWwindow* window_;
    Settings settings_;
    VmRepository repo_;
    HostInfo host_;
    mutable std::mutex backendMutex_;
    std::shared_ptr<const QemuBackend> backend_;
    StatusMonitor monitor_;
    JobRunner jobs_;
    std::vector<VmStatus> statuses_;  // this frame's copy of the monitor snapshot

    // Navigation
    Page page_ = Page::Home;
    std::string selectedVm_;
    VmTab vmTab_ = VmTab::Summary;
    bool forceTab_ = false;
    std::map<std::string, VmState> lastStates_;  // to switch tabs when a VM powers on/off
    std::string librarySearch_;
    bool showLibrary_ = true;

    // Full screen
    bool fullscreen_ = false;
    int windowedX_ = 0, windowedY_ = 0, windowedW_ = 1280, windowedH_ = 800;

    // VM screens and snapshots, keyed by VM name
    std::map<std::string, std::unique_ptr<VmScreen>> screens_;
    std::map<std::string, std::vector<SnapshotInfo>> snapshots_;
    std::string snapshotName_;

    // New VM wizard
    bool showWizard_ = false;
    int wizardStep_ = 0;
    int wizardSource_ = 0;  // 0 = ISO, 1 = install later
    std::string wizardIso_;
    OsGuess wizardGuess_;
    int wizardOs_ = 0;
    std::string wizardName_;
    int wizardDisk_ = 20, wizardMemory_ = 2048, wizardCpus_ = 2;
    bool wizardPowerOn_ = true;

    // Virtual machine settings dialog
    bool showVmSettings_ = false;
    SettingsPage settingsPage_ = SettingsPage::Memory;
    VmConfig vmEdit_;
    int expandTo_ = 0;
    std::string newSegment_;

    // Import / clone
    bool showImport_ = false;
    std::string importPath_, importName_;
    int importOs_ = 0, importMemory_ = 2048, importCpus_ = 2;
    bool showClone_ = false;
    std::string cloneSource_, cloneName_;

    // Other dialogs
    bool showPreferences_ = false;
    Settings prefsEdit_;
    bool showDiagnostics_ = false;
    std::vector<DiagnosticItem> diagnostics_;
    bool showAbout_ = false;
    bool showConfirm_ = false;
    std::string confirmTitle_, confirmText_, confirmAction_;
    std::function<void()> confirmCallback_;
    bool showExit_ = false;
    bool exitApproved_ = false;

    std::optional<UpdateInfo> update_;

    struct Toast {
        std::string text;
        bool error;
        double expires;
    };
    std::vector<Toast> toasts_;
};

}  // namespace eduvm::app
