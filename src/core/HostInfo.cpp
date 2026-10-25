#include "core/HostInfo.hpp"

#include <algorithm>
#include <thread>

#include "core/Paths.hpp"

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#include <unistd.h>
#else
#include <sys/sysinfo.h>
#include <unistd.h>
#endif

namespace eduvm {

std::string toString(Accelerator a) {
    switch (a) {
        case Accelerator::Whpx: return "whpx";
        case Accelerator::Kvm: return "kvm";
        case Accelerator::Hvf: return "hvf";
        case Accelerator::None: return "none";
    }
    return "none";
}

#ifdef _WIN32
// Asks the Windows Hypervisor Platform directly (no admin rights needed).
static bool whpxAvailable(std::string& detail) {
    HMODULE lib = LoadLibraryW(L"WinHvPlatform.dll");
    if (!lib) {
        detail = "Windows Hypervisor Platform is not installed. Enable it via "
                 "'Turn Windows features on or off' > 'Windows Hypervisor Platform', then reboot.";
        return false;
    }
    using GetCapabilityFn = HRESULT(WINAPI*)(int, void*, UINT32, UINT32*);
    auto getCapability = reinterpret_cast<GetCapabilityFn>(GetProcAddress(lib, "WHvGetCapability"));
    BOOL present = FALSE;
    UINT32 written = 0;
    bool ok = getCapability && SUCCEEDED(getCapability(0 /* HypervisorPresent */, &present, sizeof(present), &written)) &&
              present;
    FreeLibrary(lib);
    detail = ok ? "Windows Hypervisor Platform is active"
                : "WHPX is installed but the hypervisor is not running. Check that virtualization (VT-x/AMD-V) "
                  "is enabled in the BIOS/UEFI.";
    return ok;
}
#endif

HostInfo HostInfo::detect() {
    HostInfo info;
    info.logicalCpus = std::max(1u, std::thread::hardware_concurrency());

#ifdef _WIN32
    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) {
        info.totalRamMb = mem.ullTotalPhys / (1024 * 1024);
        info.availableRamMb = mem.ullAvailPhys / (1024 * 1024);
    }
    info.accelerator = whpxAvailable(info.acceleratorDetail) ? Accelerator::Whpx : Accelerator::None;
#elif defined(__APPLE__)
    std::uint64_t memBytes = 0;
    size_t len = sizeof(memBytes);
    if (sysctlbyname("hw.memsize", &memBytes, &len, nullptr, 0) == 0) info.totalRamMb = memBytes / (1024 * 1024);
    info.availableRamMb = info.totalRamMb / 2;  // macOS has no cheap "available" figure
    int hv = 0;
    len = sizeof(hv);
    bool hvf = sysctlbyname("kern.hv_support", &hv, &len, nullptr, 0) == 0 && hv == 1;
    info.accelerator = hvf ? Accelerator::Hvf : Accelerator::None;
    info.acceleratorDetail = hvf ? "Hypervisor.framework is available" : "Hypervisor.framework is not supported";
#else
    struct sysinfo si {};
    if (sysinfo(&si) == 0) {
        info.totalRamMb = static_cast<std::uint64_t>(si.totalram) * si.mem_unit / (1024 * 1024);
        info.availableRamMb = static_cast<std::uint64_t>(si.freeram + si.bufferram) * si.mem_unit / (1024 * 1024);
    }
    if (access("/dev/kvm", R_OK | W_OK) == 0) {
        info.accelerator = Accelerator::Kvm;
        info.acceleratorDetail = "/dev/kvm is accessible";
    } else {
        info.acceleratorDetail = access("/dev/kvm", F_OK) == 0
                                     ? "/dev/kvm exists but you lack permission: sudo usermod -aG kvm $USER, then log in again"
                                     : "/dev/kvm not found: enable VT-x/AMD-V in BIOS and load the kvm module";
    }
#endif

    std::error_code ec;
    auto space = std::filesystem::space(dataDir(), ec);
    if (!ec) info.freeDiskMb = space.available / (1024 * 1024);
    return info;
}

ResourceAdvice recommendResources(const HostInfo& host, int requestedCpus, int requestedMemoryMb) {
    ResourceAdvice advice{requestedCpus, requestedMemoryMb, {}};

    int maxCpus = static_cast<int>(std::max(1u, host.logicalCpus - (host.logicalCpus > 2 ? 1u : 0u)));
    if (advice.cpus > maxCpus) {
        advice.warnings.push_back("Requested " + std::to_string(advice.cpus) + " CPUs but the host has " +
                                  std::to_string(host.logicalCpus) + "; using " + std::to_string(maxCpus) + ".");
        advice.cpus = maxCpus;
    }

    if (host.totalRamMb > 0) {
        // Never give a VM more than 60% of physical RAM.
        int cap = static_cast<int>(host.totalRamMb * 6 / 10) / 256 * 256;
        if (advice.memoryMb > cap) {
            advice.warnings.push_back("Requested " + std::to_string(advice.memoryMb) + " MB RAM; capped to " +
                                      std::to_string(cap) + " MB so your computer stays usable.");
            advice.memoryMb = cap;
        }
        if (host.availableRamMb > 0 && static_cast<std::uint64_t>(advice.memoryMb) > host.availableRamMb) {
            advice.warnings.push_back("Only " + std::to_string(host.availableRamMb) +
                                      " MB RAM is free right now. Close other apps (browsers!) or the VM may be slow.");
        }
    }
    if (host.accelerator == Accelerator::None) {
        advice.warnings.push_back("No hardware acceleration: VMs will run in slow emulation mode. See Help > System check.");
    }
    return advice;
}

}  // namespace eduvm
