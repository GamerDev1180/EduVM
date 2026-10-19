#pragma once

#include <map>
#include <nlohmann/json.hpp>
#include <string>

#include "core/Lab.hpp"

namespace eduvm {

struct TaskProgress {
    bool completed = false;
    std::string completedAt;
    int attempts = 0;       // grading runs
    int hintsRevealed = 0;  // instructor hints shown
    int aiQuestions = 0;    // questions asked to the AI tutor
    int pointsEarned = 0;
    std::string lastFeedback;  // failed checks from the latest attempt, fed to the AI tutor
};

// Per-lab progress (progress/<labId>.json). Hint and AI usage are recorded
// on purpose: teachers see *how* a student got there, not just the score.
struct LabProgress {
    std::string labId;
    std::string labVersion;
    std::string startedAt;
    std::map<std::string, TaskProgress> tasks;

    static LabProgress load(const std::string& labId);
    void save() const;

    TaskProgress& task(const std::string& taskId) { return tasks[taskId]; }
    int totalPoints() const;
};

void to_json(nlohmann::json& j, const LabProgress& p);
void from_json(const nlohmann::json& j, LabProgress& p);

// Markdown report a student can hand in.
std::string renderReport(const Lab& lab, const LabProgress& progress, const std::string& studentName);

}  // namespace eduvm
