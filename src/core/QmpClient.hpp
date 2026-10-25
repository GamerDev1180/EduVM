#pragma once

#include <nlohmann/json.hpp>
#include <string>

#include "core/TcpSocket.hpp"

namespace eduvm {

// Client for the QEMU Machine Protocol: the JSON control channel of a
// running VM (pause, resume, power off, live snapshots, screenshots).
class QmpClient {
public:
    void connect(int port);
    // Commands such as savevm can legitimately take a long time.
    void setTimeout(std::chrono::milliseconds t) { socket_.setReadTimeout(t); }
    nlohmann::json execute(const std::string& command, const nlohmann::json& arguments = nlohmann::json::object());
    // Runs a human-monitor (HMP) command; used for savevm/loadvm which have no
    // stable QMP equivalent yet. Returns the textual output.
    std::string humanCommand(const std::string& commandLine);

    std::string status();  // "running", "paused", "shutdown", ...
    void pause() { execute("stop"); }
    void resume() { execute("cont"); }
    void powerDown() { execute("system_powerdown"); }  // ACPI, lets the guest shut down cleanly
    void reset() { execute("system_reset"); }
    void quit() { execute("quit"); }
    void screendump(const std::string& file) { execute("screendump", {{"filename", file}}); }

private:
    TcpSocket socket_;
};

}  // namespace eduvm
