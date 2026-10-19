#include "core/Grader.hpp"

#include <map>
#include <regex>

#include "core/QemuBackend.hpp"
#include "core/Strings.hpp"

namespace eduvm {

static std::string shorten(const std::string& s, size_t max = 300) {
    std::string t = trim(s);
    return t.size() <= max ? t : t.substr(0, max) + "...";
}

CheckResult evaluateCheck(const LabCheck& check, const GuestExecResult& exec) {
    CheckResult r;
    r.description = check.description;

    if (exec.timedOut) {
        r.detail = "Command timed out after " + std::to_string(check.timeoutSeconds) + "s";
        return r;
    }
    const std::string out = trim(exec.out);

    if (check.expectExitCode && exec.exitCode != *check.expectExitCode) {
        r.detail = "Expected exit code " + std::to_string(*check.expectExitCode) + ", got " + std::to_string(exec.exitCode);
        if (!trim(exec.err).empty()) r.detail += " (stderr: " + shorten(exec.err) + ")";
        return r;
    }
    if (check.expectOutput && out != trim(*check.expectOutput)) {
        r.detail = "Expected output '" + trim(*check.expectOutput) + "', got '" + shorten(out) + "'";
        return r;
    }
    if (check.expectContains && out.find(*check.expectContains) == std::string::npos) {
        r.detail = "Expected output to contain '" + *check.expectContains + "'";
        return r;
    }
    if (check.expectRegex) {
        try {
            if (!std::regex_search(out, std::regex(*check.expectRegex))) {
                r.detail = "Output did not match the expected pattern";
                return r;
            }
        } catch (const std::regex_error&) {
            r.detail = "Lab error: invalid regex in check (tell your instructor)";
            return r;
        }
    }
    r.passed = true;
    r.detail = "OK";
    return r;
}

Grader::Grader(const QemuBackend& backend, const Lab& lab) : backend_(backend), lab_(lab) {}

TaskResult Grader::grade(const LabTask& task) const {
    TaskResult result;
    result.taskId = task.id;
    std::map<std::string, GuestAgent> agents;  // one connection per VM

    bool allPassed = !task.checks.empty();
    for (const LabCheck& check : task.checks) {
        const LabVm* vm = lab_.findVm(check.vm);
        CheckResult cr;
        cr.description = check.description;
        try {
            std::string instance = lab_.instanceName(*vm);
            auto it = agents.find(instance);
            if (it == agents.end()) it = agents.emplace(instance, backend_.connectGuestAgent(instance)).first;

            GuestExecResult exec;
            if (vm->guestOs == "windows") {
                exec = it->second.exec("powershell.exe", {"-NoProfile", "-NonInteractive", "-Command", check.command},
                                       std::chrono::seconds(check.timeoutSeconds));
            } else {
                exec = it->second.exec("/bin/sh", {"-c", check.command}, std::chrono::seconds(check.timeoutSeconds));
            }
            cr = evaluateCheck(check, exec);
        } catch (const Error& e) {
            cr.passed = false;
            cr.detail = e.what();
        }
        allPassed = allPassed && cr.passed;
        result.checks.push_back(cr);
    }
    result.passed = allPassed;
    result.points = allPassed ? task.points : 0;
    return result;
}

}  // namespace eduvm
