#pragma once

#include <string>
#include <utility>
#include <vector>

#include "core/Http.hpp"
#include "core/Lab.hpp"
#include "core/QemuBackend.hpp"

namespace eduvm {

// Turns a lab manifest into real VMs and manages them as one unit.
class LabRunner {
public:
    LabRunner(const QemuBackend& backend, const VmRepository& repo, const HostInfo& host, const Lab& lab);

    // Creates any lab VMs that do not exist yet. Returns resource warnings.
    std::vector<std::string> provision(const ProgressFn& downloadProgress = {}) const;
    void startAll() const;
    void stopAll(bool force) const;
    // Throws away the student's changes by recreating the copy-on-write
    // overlay on top of the pristine base image. Empty vmName = all VMs.
    void reset(const std::string& vmName = {}) const;
    std::vector<std::pair<std::string, VmState>> status() const;

private:
    std::filesystem::path resolveImage(const std::string& file) const;
    void createDiskFor(const LabVm& vm, const VmConfig& cfg, const ProgressFn& progress) const;

    const QemuBackend& backend_;
    const VmRepository& repo_;
    const HostInfo& host_;
    const Lab& lab_;
};

}  // namespace eduvm
