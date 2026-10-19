#pragma once

#include <string>
#include <vector>

#include "core/GuestAgent.hpp"
#include "core/Lab.hpp"

namespace eduvm {

class QemuBackend;

struct CheckResult {
    std::string description;
    bool passed = false;
    std::string detail;  // what we expected vs. what we got, shown to the student
};

struct TaskResult {
    std::string taskId;
    bool passed = false;
    int points = 0;
    std::vector<CheckResult> checks;
};

// Pure comparison logic; separated so it is testable without a VM.
CheckResult evaluateCheck(const LabCheck& check, const GuestExecResult& exec);

// Runs a task's checks inside the lab VMs.
class Grader {
public:
    Grader(const QemuBackend& backend, const Lab& lab);
    TaskResult grade(const LabTask& task) const;

private:
    const QemuBackend& backend_;
    const Lab& lab_;
};

}  // namespace eduvm
