#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "core/VmConfig.hpp"

namespace eduvm {

// Stores VMs on disk:  <vmsDir>/<name>/{vm.json, disk.qcow2, runtime.json, qemu.log}
class VmRepository {
public:
    explicit VmRepository(std::filesystem::path root);

    std::vector<VmConfig> list() const;
    bool exists(const std::string& name) const;
    VmConfig load(const std::string& name) const;
    void save(const VmConfig& config) const;
    void remove(const std::string& name) const;

    std::filesystem::path vmDir(const std::string& name) const;
    std::filesystem::path diskPath(const VmConfig& config) const;
    std::filesystem::path logPath(const std::string& name) const;

    // Runtime state. loadRuntime() returns nullopt (and cleans up) if the
    // recorded QEMU process is gone, e.g. the student closed the window.
    std::optional<VmRuntime> loadRuntime(const std::string& name) const;
    void saveRuntime(const std::string& name, const VmRuntime& runtime) const;
    void clearRuntime(const std::string& name) const;

private:
    std::filesystem::path root_;
};

}  // namespace eduvm
