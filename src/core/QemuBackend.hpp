#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "core/GuestAgent.hpp"
#include "core/HostInfo.hpp"
#include "core/Settings.hpp"
#include "core/VmConfig.hpp"
#include "core/VmRepository.hpp"

namespace eduvm {

enum class VmState { Stopped, Running, Paused, Unknown };
std::string toString(VmState s);

struct SnapshotInfo {
    std::string id;
    std::string name;
    std::string date;
    bool includesRam = false;  // live snapshot (savevm) vs. disk-only
};

// Everything needed to build a QEMU command line; kept separate from the
// backend so it can be unit-tested without QEMU installed.
struct QemuLaunchSpec {
    std::filesystem::path qemuBinary;
    std::filesystem::path diskPath;
    VmConfig config;
    VmRuntime runtime;
    Accelerator accelerator = Accelerator::None;
};

std::vector<std::string> buildQemuArgs(const QemuLaunchSpec& spec);

// Lab segments map onto QEMU multicast socket networking: no admin rights or
// TAP drivers needed, and every VM joined to the same group sees the others.
int segmentMulticastPort(const std::string& segment);
std::string deterministicMac(const std::string& vmName, size_t nicIndex);

// Runs VMs through QEMU with hardware acceleration (WHPX/KVM/HVF), falling
// back to TCG emulation when acceleration is unavailable.
class QemuBackend {
public:
    QemuBackend(Settings settings, const VmRepository& repo, HostInfo host);

    std::optional<std::filesystem::path> qemuSystemBinary(const std::string& arch) const;
    std::optional<std::filesystem::path> qemuImgBinary() const;

    void createDisk(const std::filesystem::path& path, int sizeGb) const;
    // Copy-on-write clone of a lab base image: instant, and tiny on disk.
    void createOverlay(const std::filesystem::path& overlay, const std::filesystem::path& base) const;

    VmRuntime start(const VmConfig& config) const;
    void stop(const std::string& name, bool force) const;
    void pause(const std::string& name) const;
    void resume(const std::string& name) const;
    VmState state(const std::string& name) const;

    void createSnapshot(const std::string& vmName, const std::string& snapshot) const;
    void restoreSnapshot(const std::string& vmName, const std::string& snapshot) const;
    void deleteSnapshot(const std::string& vmName, const std::string& snapshot) const;
    std::vector<SnapshotInfo> listSnapshots(const std::string& vmName) const;

    GuestAgent connectGuestAgent(const std::string& vmName) const;

private:
    std::filesystem::path requireQemuImg() const;
    std::vector<std::filesystem::path> searchDirs() const;
    VmRuntime requireRunning(const std::string& name) const;

    Settings settings_;
    const VmRepository& repo_;
    HostInfo host_;
};

}  // namespace eduvm
