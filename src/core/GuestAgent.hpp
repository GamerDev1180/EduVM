#pragma once

#include <chrono>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "core/TcpSocket.hpp"

namespace eduvm {

struct GuestExecResult {
    int exitCode = -1;
    std::string out;
    std::string err;
    bool timedOut = false;
};

// Talks to qemu-guest-agent running *inside* the VM. This is what lets the
// auto-grader run "id -u student" in the guest without SSH or passwords.
// Linux: `apt install qemu-guest-agent`. Windows: virtio-win guest tools.
class GuestAgent {
public:
    void connect(int port);
    bool ping();
    GuestExecResult exec(const std::string& path, const std::vector<std::string>& args,
                         std::chrono::seconds timeout = std::chrono::seconds(30));

private:
    void sync();
    nlohmann::json call(const std::string& command, const nlohmann::json& arguments);
    TcpSocket socket_;
};

}  // namespace eduvm
