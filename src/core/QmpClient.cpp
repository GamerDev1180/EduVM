#include "core/QmpClient.hpp"

#include "core/Strings.hpp"

using nlohmann::json;

namespace eduvm {

void QmpClient::connect(int port) {
    socket_.connect("127.0.0.1", port, std::chrono::seconds(3));
    json greeting = json::parse(socket_.readLine());
    if (!greeting.contains("QMP")) throw Error("Unexpected QMP greeting");
    execute("qmp_capabilities");
}

json QmpClient::execute(const std::string& command, const json& arguments) {
    json request = {{"execute", command}};
    if (!arguments.empty()) request["arguments"] = arguments;
    socket_.sendAll(request.dump() + "\n");

    while (true) {
        json reply = json::parse(socket_.readLine());
        if (reply.contains("event")) continue;  // async events are interleaved; skip them
        if (reply.contains("error")) {
            throw Error("QEMU rejected '" + command + "': " + reply["error"].value("desc", std::string("unknown error")));
        }
        if (reply.contains("return")) return reply["return"];
    }
}

std::string QmpClient::humanCommand(const std::string& commandLine) {
    json out = execute("human-monitor-command", {{"command-line", commandLine}});
    std::string text = out.is_string() ? out.get<std::string>() : std::string();
    // HMP reports failures as text, not as a QMP error.
    std::string lower = toLower(text);
    if (lower.find("error") != std::string::npos) throw Error(trim(text));
    return text;
}

std::string QmpClient::status() {
    json s = execute("query-status");
    return s.value("status", std::string("unknown"));
}

}  // namespace eduvm
