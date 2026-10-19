#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace eduvm {

enum class Accelerator { Whpx, Kvm, Hvf, None };

struct HostInfo {
    unsigned logicalCpus = 1;
    std::uint64_t totalRamMb = 0;
    std::uint64_t availableRamMb = 0;
    std::uint64_t freeDiskMb = 0;  // on the drive holding EduVM data
    Accelerator accelerator = Accelerator::None;
    std::string acceleratorDetail;

    static HostInfo detect();
};

std::string toString(Accelerator a);

struct ResourceAdvice {
    int cpus;
    int memoryMb;
    std::vector<std::string> warnings;
};

// Laptop-friendly defaults: students often have 8 GB machines, and a VM that
// starves the host makes the whole experience feel broken.
ResourceAdvice recommendResources(const HostInfo& host, int requestedCpus, int requestedMemoryMb);

}  // namespace eduvm
