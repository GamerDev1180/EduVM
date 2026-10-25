#include "core/Diagnostics.hpp"

#include "core/Paths.hpp"
#include "core/Strings.hpp"

namespace eduvm {

std::vector<DiagnosticItem> runDiagnostics(const Settings&, const QemuBackend& backend, const HostInfo& host) {
    std::vector<DiagnosticItem> items;

    auto qemu = backend.qemuSystemBinary("x86_64");
    items.push_back({"QEMU", qemu.has_value(), false,
                     qemu ? pathToUtf8(*qemu)
                          : "Not found. Install it from https://www.qemu.org/download/ (Windows: winget install "
                            "SoftwareFreedomConservancy.QEMU), or set the QEMU folder in Settings."});

    auto img = backend.qemuImgBinary();
    items.push_back({"qemu-img", img.has_value(), false, img ? pathToUtf8(*img) : "Not found (it ships with QEMU)."});

    items.push_back({"Hardware acceleration", host.accelerator != Accelerator::None, false,
                     toString(host.accelerator) + ": " + host.acceleratorDetail});

    items.push_back({"Memory", host.totalRamMb >= 8192, true,
                     std::to_string(host.totalRamMb / 1024) + " GB total, " + std::to_string(host.availableRamMb / 1024) +
                         " GB free" + (host.totalRamMb < 8192 ? " (8 GB or more recommended)" : "")});

    items.push_back({"CPU", host.logicalCpus >= 4, true, std::to_string(host.logicalCpus) + " logical cores"});

    items.push_back({"Disk space", host.freeDiskMb >= 20480, true,
                     std::to_string(host.freeDiskMb / 1024) + " GB free in " + pathToUtf8(dataDir())});

    return items;
}

}  // namespace eduvm
