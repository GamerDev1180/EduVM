#include "core/QemuBackend.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <nlohmann/json.hpp>
#include <thread>

#include "core/Paths.hpp"
#include "core/Process.hpp"
#include "core/QmpClient.hpp"
#include "core/Strings.hpp"
#include "core/TcpSocket.hpp"

namespace fs = std::filesystem;
using nlohmann::json;

namespace eduvm {

std::string toString(VmState s) {
    switch (s) {
        case VmState::Stopped: return "stopped";
        case VmState::Running: return "running";
        case VmState::Paused: return "paused";
        case VmState::Unknown: return "unknown";
    }
    return "unknown";
}

static std::uint32_t fnv1a(std::string_view s) {
    std::uint32_t h = 2166136261u;
    for (unsigned char c : s) {
        h ^= c;
        h *= 16777619u;
    }
    return h;
}

int segmentMulticastPort(const std::string& segment) { return 20000 + static_cast<int>(fnv1a(segment) % 10000); }

std::string deterministicMac(const std::string& vmName, size_t nicIndex) {
    std::uint32_t h = fnv1a(vmName + "#" + std::to_string(nicIndex));
    char buf[18];
    std::snprintf(buf, sizeof(buf), "52:54:00:%02x:%02x:%02x", (h >> 16) & 0xFF, (h >> 8) & 0xFF, h & 0xFF);
    return buf;
}

std::vector<std::string> buildQemuArgs(const QemuLaunchSpec& spec) {
    const VmConfig& c = spec.config;
    const bool windowsGuest = c.guestOs == GuestOs::Windows;
    std::vector<std::string> a = {pathToUtf8(spec.qemuBinary), "-name", c.name};

    if (c.arch == "x86_64") a.insert(a.end(), {"-machine", "q35"});
    else if (c.arch == "aarch64") a.insert(a.end(), {"-machine", "virt"});

    // Multiple -accel options are tried in order, so TCG is a safety net.
    switch (spec.accelerator) {
        case Accelerator::Whpx: a.insert(a.end(), {"-accel", "whpx,kernel-irqchip=off"}); break;
        case Accelerator::Kvm: a.insert(a.end(), {"-accel", "kvm"}); break;
        case Accelerator::Hvf: a.insert(a.end(), {"-accel", "hvf"}); break;
        case Accelerator::None: break;
    }
    a.insert(a.end(), {"-accel", "tcg"});

    std::string cpu = c.cpuModel;
    if (cpu.empty()) {
        cpu = (spec.accelerator == Accelerator::Kvm || spec.accelerator == Accelerator::Hvf) ? "host" : "max";
    }
    a.insert(a.end(), {"-cpu", cpu, "-smp", std::to_string(c.cpus), "-m", std::to_string(c.memoryMb)});

    // Windows has no in-box virtio drivers; give it AHCI + e1000 so installs just work.
    std::string disk = "file=" + pathToUtf8(spec.diskPath) + ",format=qcow2,if=" + (windowsGuest ? "ide" : "virtio");
    a.insert(a.end(), {"-drive", disk});
    if (!c.isoPath.empty()) a.insert(a.end(), {"-cdrom", c.isoPath, "-boot", "order=dc,menu=on"});

    const std::string nicModel = windowsGuest ? "e1000" : "virtio-net-pci";
    std::string userNet = "user,id=net0";
    if (spec.runtime.sshPort > 0) userNet += ",hostfwd=tcp:127.0.0.1:" + std::to_string(spec.runtime.sshPort) + "-:22";
    a.insert(a.end(), {"-netdev", userNet, "-device", nicModel + ",netdev=net0,mac=" + deterministicMac(c.name, 0)});

    for (size_t i = 0; i < c.segments.size(); ++i) {
        std::string id = "seg" + std::to_string(i);
        std::string netdev = "socket,id=" + id + ",mcast=230.0.0.1:" + std::to_string(segmentMulticastPort(c.segments[i])) +
                             ",localaddr=127.0.0.1";
        a.insert(a.end(), {"-netdev", netdev, "-device", nicModel + ",netdev=" + id + ",mac=" + deterministicMac(c.name, i + 1)});
    }

    a.insert(a.end(), {"-qmp", "tcp:127.0.0.1:" + std::to_string(spec.runtime.qmpPort) + ",server=on,wait=off"});
    a.insert(a.end(), {"-chardev",
                       "socket,id=qga0,host=127.0.0.1,port=" + std::to_string(spec.runtime.qgaPort) + ",server=on,wait=off",
                       "-device", "virtio-serial", "-device",
                       "virtserialport,chardev=qga0,name=org.qemu.guest_agent.0"});

    a.insert(a.end(), {"-usb", "-device", "usb-tablet"});  // absolute pointer: no mouse grabbing
    if (windowsGuest) a.insert(a.end(), {"-rtc", "base=localtime"});
    if (c.display == DisplayMode::Headless) a.insert(a.end(), {"-display", "none"});
    if (c.display == DisplayMode::Embedded) {
        // VNC display numbers are offsets from port 5900.
        a.insert(a.end(), {"-display", "none", "-vnc", "127.0.0.1:" + std::to_string(spec.runtime.vncPort - 5900)});
    }
    return a;
}

QemuBackend::QemuBackend(Settings settings, const VmRepository& repo, HostInfo host)
    : settings_(std::move(settings)), repo_(repo), host_(std::move(host)) {}

std::vector<fs::path> QemuBackend::searchDirs() const {
    std::vector<fs::path> dirs;
    if (!settings_.qemuDir.empty()) dirs.push_back(utf8ToPath(settings_.qemuDir));
#ifdef _WIN32
    dirs.emplace_back("C:\\Program Files\\qemu");
#elif defined(__APPLE__)
    dirs.emplace_back("/opt/homebrew/bin");
    dirs.emplace_back("/usr/local/bin");
#endif
    return dirs;
}

std::optional<fs::path> QemuBackend::qemuSystemBinary(const std::string& arch) const {
    return findExecutable("qemu-system-" + arch, searchDirs());
}

std::optional<fs::path> QemuBackend::qemuImgBinary() const { return findExecutable("qemu-img", searchDirs()); }

fs::path QemuBackend::requireQemuImg() const {
    auto img = qemuImgBinary();
    if (!img) throw Error("qemu-img not found. Install QEMU or set its folder with 'eduvm config set qemuDir <path>'.");
    return *img;
}

void QemuBackend::createDisk(const fs::path& path, int sizeGb) const {
    auto r = runAndCapture({pathToUtf8(requireQemuImg()), "create", "-f", "qcow2", pathToUtf8(path),
                            std::to_string(sizeGb) + "G"});
    if (r.exitCode != 0) throw Error("qemu-img create failed: " + trim(r.output));
}

void QemuBackend::createOverlay(const fs::path& overlay, const fs::path& base) const {
    if (!fs::exists(base)) throw Error("Base image not found: " + pathToUtf8(base));
    auto r = runAndCapture({pathToUtf8(requireQemuImg()), "create", "-f", "qcow2", "-F", "qcow2", "-b",
                            pathToUtf8(fs::absolute(base)), pathToUtf8(overlay)});
    if (r.exitCode != 0) throw Error("qemu-img create (overlay) failed: " + trim(r.output));
}

VmRuntime QemuBackend::start(const VmConfig& config) const {
    if (repo_.loadRuntime(config.name)) throw Error("VM '" + config.name + "' is already running.");

    auto binary = qemuSystemBinary(config.arch);
    if (!binary) {
        throw Error("qemu-system-" + config.arch +
                    " not found. Install QEMU (https://www.qemu.org/download/) or run 'eduvm doctor'.");
    }
    fs::path disk = repo_.diskPath(config);
    if (!fs::exists(disk)) throw Error("Disk image missing: " + pathToUtf8(disk));
    if (!config.isoPath.empty() && !fs::exists(utf8ToPath(config.isoPath)))
        throw Error("ISO not found: " + config.isoPath);

    VmRuntime rt;
    rt.qmpPort = findFreePort();
    rt.qgaPort = findFreePort();
    rt.sshPort = config.forwardSsh ? findFreePort() : 0;
    rt.vncPort = config.display == DisplayMode::Embedded ? findFreePortInRange(5901, 6999) : 0;
    rt.startedAt = nowIso8601();

    QemuLaunchSpec spec{*binary, disk, config, rt, host_.accelerator};
    rt.pid = spawnDetached(buildQemuArgs(spec), repo_.logPath(config.name));

    // Wait until QMP answers so callers can rely on the VM being controllable.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (true) {
        if (!isProcessAlive(rt.pid)) {
            std::string log;
            try {
                log = trim(readFile(repo_.logPath(config.name)));
            } catch (const Error&) {
            }
            throw Error("QEMU exited during startup.\n" + log);
        }
        try {
            QmpClient qmp;
            qmp.connect(rt.qmpPort);
            break;
        } catch (const Error&) {
            if (std::chrono::steady_clock::now() > deadline) {
                killProcess(rt.pid);
                throw Error("QEMU did not become ready within 15 seconds; see " + pathToUtf8(repo_.logPath(config.name)));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }
    repo_.saveRuntime(config.name, rt);
    return rt;
}

VmRuntime QemuBackend::requireRunning(const std::string& name) const {
    auto rt = repo_.loadRuntime(name);
    if (!rt) throw Error("VM '" + name + "' is not running.");
    return *rt;
}

void QemuBackend::stop(const std::string& name, bool force) const {
    VmRuntime rt = requireRunning(name);
    try {
        QmpClient qmp;
        qmp.connect(rt.qmpPort);
        if (force) qmp.quit();
        else qmp.powerDown();
    } catch (const Error&) {
        if (!force) throw;
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(force ? 5 : 90);
    while (isProcessAlive(rt.pid) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(250));

    if (isProcessAlive(rt.pid)) {
        if (!force) {
            throw Error("The guest did not shut down within 90 seconds (it may be ignoring ACPI). "
                        "Use 'eduvm vm stop " + name + " --force' to pull the plug.");
        }
        killProcess(rt.pid);
    }
    repo_.clearRuntime(name);
}

void QemuBackend::pause(const std::string& name) const {
    QmpClient qmp;
    qmp.connect(requireRunning(name).qmpPort);
    qmp.pause();
}

void QemuBackend::resume(const std::string& name) const {
    QmpClient qmp;
    qmp.connect(requireRunning(name).qmpPort);
    qmp.resume();
}

VmState QemuBackend::state(const std::string& name) const {
    auto rt = repo_.loadRuntime(name);
    if (!rt) return VmState::Stopped;
    try {
        QmpClient qmp;
        qmp.connect(rt->qmpPort);
        std::string s = qmp.status();
        if (s == "running") return VmState::Running;
        if (s == "paused") return VmState::Paused;
        return VmState::Unknown;
    } catch (const Error&) {
        return VmState::Unknown;
    }
}

// Running VMs get live snapshots (RAM + devices, resume exactly where you
// were). Stopped VMs get disk-only snapshots via qemu-img.
void QemuBackend::createSnapshot(const std::string& vmName, const std::string& snapshot) const {
    if (!isValidVmName(snapshot)) throw Error("Snapshot names may contain letters, digits, - and _ only.");
    if (auto rt = repo_.loadRuntime(vmName)) {
        QmpClient qmp;
        qmp.connect(rt->qmpPort);
        qmp.humanCommand("savevm " + snapshot);
        return;
    }
    VmConfig cfg = repo_.load(vmName);
    auto r = runAndCapture({pathToUtf8(requireQemuImg()), "snapshot", "-c", snapshot, pathToUtf8(repo_.diskPath(cfg))});
    if (r.exitCode != 0) throw Error("Snapshot failed: " + trim(r.output));
}

void QemuBackend::restoreSnapshot(const std::string& vmName, const std::string& snapshot) const {
    if (auto rt = repo_.loadRuntime(vmName)) {
        QmpClient qmp;
        qmp.connect(rt->qmpPort);
        qmp.humanCommand("loadvm " + snapshot);
        return;
    }
    VmConfig cfg = repo_.load(vmName);
    auto r = runAndCapture({pathToUtf8(requireQemuImg()), "snapshot", "-a", snapshot, pathToUtf8(repo_.diskPath(cfg))});
    if (r.exitCode != 0) throw Error("Restore failed: " + trim(r.output));
}

void QemuBackend::deleteSnapshot(const std::string& vmName, const std::string& snapshot) const {
    if (auto rt = repo_.loadRuntime(vmName)) {
        QmpClient qmp;
        qmp.connect(rt->qmpPort);
        qmp.humanCommand("delvm " + snapshot);
        return;
    }
    VmConfig cfg = repo_.load(vmName);
    auto r = runAndCapture({pathToUtf8(requireQemuImg()), "snapshot", "-d", snapshot, pathToUtf8(repo_.diskPath(cfg))});
    if (r.exitCode != 0) throw Error("Delete failed: " + trim(r.output));
}

std::vector<SnapshotInfo> QemuBackend::listSnapshots(const std::string& vmName) const {
    VmConfig cfg = repo_.load(vmName);
    // -U (force share) lets us read the image while the VM has it open.
    auto r = runAndCapture({pathToUtf8(requireQemuImg()), "info", "-U", "--output=json", pathToUtf8(repo_.diskPath(cfg))});
    if (r.exitCode != 0) throw Error("qemu-img info failed: " + trim(r.output));

    std::vector<SnapshotInfo> out;
    json info = json::parse(r.output);
    for (auto& s : info.value("snapshots", json::array())) {
        SnapshotInfo si;
        si.id = s.value("id", std::string());
        si.name = s.value("name", std::string());
        si.includesRam = s.value("vm-state-size", 0LL) > 0;
        std::time_t t = static_cast<std::time_t>(s.value("date-sec", 0LL));
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
        si.date = buf;
        out.push_back(si);
    }
    return out;
}

GuestAgent QemuBackend::connectGuestAgent(const std::string& vmName) const {
    VmRuntime rt = requireRunning(vmName);
    GuestAgent agent;
    try {
        agent.connect(rt.qgaPort);
    } catch (const Error& e) {
        throw Error(std::string(e.what()) +
                    "\nIs qemu-guest-agent installed and running inside '" + vmName +
                    "'? (Linux: sudo apt install qemu-guest-agent && sudo systemctl start qemu-guest-agent)");
    }
    return agent;
}

}  // namespace eduvm
