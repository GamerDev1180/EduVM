#include "core/VmRepository.hpp"

#include <algorithm>

#include "core/Paths.hpp"
#include "core/Process.hpp"
#include "core/Strings.hpp"

namespace fs = std::filesystem;
using nlohmann::json;

namespace eduvm {

VmRepository::VmRepository(fs::path root) : root_(std::move(root)) {}

fs::path VmRepository::vmDir(const std::string& name) const {
    if (!isValidVmName(name)) throw Error("Invalid VM name '" + name + "' (use letters, digits, - and _)");
    return root_ / name;
}

fs::path VmRepository::diskPath(const VmConfig& config) const { return vmDir(config.name) / config.diskFile; }

fs::path VmRepository::logPath(const std::string& name) const { return vmDir(name) / "qemu.log"; }

std::vector<VmConfig> VmRepository::list() const {
    std::vector<VmConfig> vms;
    std::error_code ec;
    for (auto& entry : fs::directory_iterator(root_, ec)) {
        if (!entry.is_directory() || !fs::exists(entry.path() / "vm.json")) continue;
        try {
            vms.push_back(json::parse(readFile(entry.path() / "vm.json")).get<VmConfig>());
        } catch (const std::exception&) {
            // A corrupt vm.json should not hide every other VM.
        }
    }
    std::sort(vms.begin(), vms.end(), [](const VmConfig& a, const VmConfig& b) { return a.name < b.name; });
    return vms;
}

bool VmRepository::exists(const std::string& name) const { return fs::exists(vmDir(name) / "vm.json"); }

VmConfig VmRepository::load(const std::string& name) const {
    fs::path file = vmDir(name) / "vm.json";
    if (!fs::exists(file)) throw Error("No VM named '" + name + "'. Run 'eduvm vm list' to see your VMs.");
    return json::parse(readFile(file)).get<VmConfig>();
}

void VmRepository::save(const VmConfig& config) const {
    writeFileAtomic(vmDir(config.name) / "vm.json", json(config).dump(2));
}

void VmRepository::remove(const std::string& name) const {
    std::error_code ec;
    fs::remove_all(vmDir(name), ec);
    if (ec) throw Error("Could not delete VM folder: " + ec.message());
}

std::optional<VmRuntime> VmRepository::loadRuntime(const std::string& name) const {
    fs::path file = vmDir(name) / "runtime.json";
    if (!fs::exists(file)) return std::nullopt;
    auto runtime = json::parse(readFile(file)).get<VmRuntime>();
    if (runtime.pid <= 0 || !isProcessAlive(runtime.pid)) {
        clearRuntime(name);
        return std::nullopt;
    }
    return runtime;
}

void VmRepository::saveRuntime(const std::string& name, const VmRuntime& runtime) const {
    writeFileAtomic(vmDir(name) / "runtime.json", json(runtime).dump(2));
}

void VmRepository::clearRuntime(const std::string& name) const {
    std::error_code ec;
    fs::remove(vmDir(name) / "runtime.json", ec);
}

}  // namespace eduvm
