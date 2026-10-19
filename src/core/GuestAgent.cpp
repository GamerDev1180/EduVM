#include <nlohmann/json.hpp>

#include "core/GuestAgent.hpp"

#include <random>
#include <thread>

#include "core/Strings.hpp"

using nlohmann::json;

namespace eduvm {

void GuestAgent::connect(int port) {
    socket_.connect("127.0.0.1", port, std::chrono::seconds(3));
    sync();
}

// The agent channel may contain stale replies from an earlier client;
// guest-sync with a random id flushes them.
void GuestAgent::sync() {
    std::mt19937 rng(std::random_device{}());
    long long id = std::uniform_int_distribution<long long>(1, 1'000'000'000)(rng);
    socket_.sendAll(json{{"execute", "guest-sync"}, {"arguments", {{"id", id}}}}.dump() + "\n");
    socket_.setReadTimeout(std::chrono::seconds(5));
    for (int i = 0; i < 50; ++i) {
        std::string line = trim(socket_.readLine());
        if (line.empty()) continue;
        json reply = json::parse(line, nullptr, false);
        if (!reply.is_discarded() && reply.contains("return") && reply["return"] == id) return;
    }
    throw Error("Guest agent did not answer the sync request");
}

json GuestAgent::call(const std::string& command, const json& arguments) {
    json request = {{"execute", command}};
    if (!arguments.empty()) request["arguments"] = arguments;
    socket_.sendAll(request.dump() + "\n");
    json reply = json::parse(socket_.readLine());
    if (reply.contains("error"))
        throw Error("Guest agent: " + reply["error"].value("desc", std::string("unknown error")));
    return reply.value("return", json());
}

bool GuestAgent::ping() {
    try {
        call("guest-ping", json::object());
        return true;
    } catch (const Error&) {
        return false;
    }
}

GuestExecResult GuestAgent::exec(const std::string& path, const std::vector<std::string>& args,
                                 std::chrono::seconds timeout) {
    json started = call("guest-exec", {{"path", path}, {"arg", args}, {"capture-output", true}});
    long long pid = started.at("pid").get<long long>();

    GuestExecResult result;
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        json status = call("guest-exec-status", {{"pid", pid}});
        if (status.value("exited", false)) {
            result.exitCode = status.value("exitcode", -1);
            result.out = base64Decode(status.value("out-data", std::string()));
            result.err = base64Decode(status.value("err-data", std::string()));
            return result;
        }
        if (std::chrono::steady_clock::now() > deadline) {
            result.timedOut = true;
            return result;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

}  // namespace eduvm
