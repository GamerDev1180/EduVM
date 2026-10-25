#pragma once

#include <string>
#include <vector>

#include "core/HostInfo.hpp"
#include "core/QemuBackend.hpp"
#include "core/Settings.hpp"

namespace eduvm {

struct DiagnosticItem {
    std::string name;
    bool ok = false;
    bool optional = false;  // a failure here doesn't block running VMs
    std::string detail;
};

// "Is this computer ready?" check shown on first launch and in the Help menu.
std::vector<DiagnosticItem> runDiagnostics(const Settings& settings, const QemuBackend& backend, const HostInfo& host);

}  // namespace eduvm
