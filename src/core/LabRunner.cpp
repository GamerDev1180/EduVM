#include "core/LabRunner.hpp"

#include "core/Paths.hpp"
#include "core/Strings.hpp"

namespace fs = std::filesystem;

namespace eduvm {

LabRunner::LabRunner(const QemuBackend& backend, const VmRepository& repo, const HostInfo& host, const Lab& lab)
    : backend_(backend), repo_(repo), host_(host), lab_(lab) {}

fs::path LabRunner::resolveImage(const std::string& file) const {
    fs::path p = utf8ToPath(file);
    return p.is_absolute() ? p : imagesDir() / p;
}

void LabRunner::createDiskFor(const LabVm& vm, const VmConfig& cfg, const ProgressFn& progress) const {
    fs::path disk = repo_.diskPath(cfg);
    if (vm.baseImage.empty()) {
        backend_.createDisk(disk, vm.diskGb);
        return;
    }
    fs::path base = resolveImage(vm.baseImage);
    if (!fs::exists(base)) {
        if (vm.imageUrl.empty())
            throw Error("Base image '" + pathToUtf8(base) + "' is missing and the lab gives no imageUrl to download it from.");
        httpDownload(vm.imageUrl, base, progress);
    }
    backend_.createOverlay(disk, base);
}

std::vector<std::string> LabRunner::provision(const ProgressFn& downloadProgress) const {
    std::vector<std::string> warnings;
    for (const LabVm& vm : lab_.vms) {
        std::string instance = lab_.instanceName(vm);
        if (repo_.exists(instance)) continue;

        auto advice = recommendResources(host_, vm.cpus, vm.memoryMb);
        for (auto& w : advice.warnings) warnings.push_back(vm.name + ": " + w);

        VmConfig cfg;
        cfg.name = instance;
        cfg.description = lab_.title + " (" + vm.name + ")";
        cfg.guestOs = guestOsFromString(vm.guestOs);
        cfg.cpus = advice.cpus;
        cfg.memoryMb = advice.memoryMb;
        cfg.diskGb = vm.diskGb;
        cfg.display = displayModeFromString(vm.display);
        cfg.labId = lab_.id;
        cfg.createdAt = nowIso8601();
        if (!vm.iso.empty()) cfg.isoPath = pathToUtf8(resolveImage(vm.iso));
        for (auto& s : vm.segments) cfg.segments.push_back(lab_.segmentName(s));

        repo_.save(cfg);
        try {
            createDiskFor(vm, cfg, downloadProgress);
        } catch (...) {
            repo_.remove(instance);  // don't leave a VM without a disk behind
            throw;
        }
    }
    return warnings;
}

void LabRunner::startAll() const {
    for (const LabVm& vm : lab_.vms) {
        std::string instance = lab_.instanceName(vm);
        if (backend_.state(instance) == VmState::Stopped) backend_.start(repo_.load(instance));
    }
}

void LabRunner::stopAll(bool force) const {
    for (const LabVm& vm : lab_.vms) {
        std::string instance = lab_.instanceName(vm);
        if (repo_.exists(instance) && backend_.state(instance) != VmState::Stopped) backend_.stop(instance, force);
    }
}

void LabRunner::reset(const std::string& vmName) const {
    for (const LabVm& vm : lab_.vms) {
        if (!vmName.empty() && vm.name != vmName) continue;
        std::string instance = lab_.instanceName(vm);
        if (!repo_.exists(instance)) continue;
        if (backend_.state(instance) != VmState::Stopped) backend_.stop(instance, true);

        VmConfig cfg = repo_.load(instance);
        std::error_code ec;
        fs::remove(repo_.diskPath(cfg), ec);
        createDiskFor(vm, cfg, {});
    }
    if (!vmName.empty() && !lab_.findVm(vmName)) throw Error("Lab has no VM named '" + vmName + "'");
}

std::vector<std::pair<std::string, VmState>> LabRunner::status() const {
    std::vector<std::pair<std::string, VmState>> out;
    for (const LabVm& vm : lab_.vms) {
        std::string instance = lab_.instanceName(vm);
        out.emplace_back(vm.name, repo_.exists(instance) ? backend_.state(instance) : VmState::Unknown);
    }
    return out;
}

}  // namespace eduvm
