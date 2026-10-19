#include "core/Progress.hpp"

#include <filesystem>
#include <sstream>

#include "core/Paths.hpp"
#include "core/Strings.hpp"

using nlohmann::json;

namespace eduvm {

void to_json(json& j, const LabProgress& p) {
    json tasks = json::object();
    for (auto& [id, t] : p.tasks) {
        tasks[id] = {{"completed", t.completed},         {"completedAt", t.completedAt}, {"attempts", t.attempts},
                     {"hintsRevealed", t.hintsRevealed}, {"aiQuestions", t.aiQuestions}, {"pointsEarned", t.pointsEarned},
                     {"lastFeedback", t.lastFeedback}};
    }
    j = {{"labId", p.labId}, {"labVersion", p.labVersion}, {"startedAt", p.startedAt}, {"tasks", tasks}};
}

void from_json(const json& j, LabProgress& p) {
    p.labId = j.value("labId", std::string());
    p.labVersion = j.value("labVersion", std::string());
    p.startedAt = j.value("startedAt", std::string());
    const json tasks = j.value("tasks", json::object());  // named: items() must not outlive a temporary
    for (auto& [id, t] : tasks.items()) {
        TaskProgress tp;
        tp.completed = t.value("completed", false);
        tp.completedAt = t.value("completedAt", std::string());
        tp.attempts = t.value("attempts", 0);
        tp.hintsRevealed = t.value("hintsRevealed", 0);
        tp.aiQuestions = t.value("aiQuestions", 0);
        tp.pointsEarned = t.value("pointsEarned", 0);
        tp.lastFeedback = t.value("lastFeedback", std::string());
        p.tasks[id] = tp;
    }
}

LabProgress LabProgress::load(const std::string& labId) {
    auto file = progressDir() / (labId + ".json");
    if (!std::filesystem::exists(file)) {
        LabProgress p;
        p.labId = labId;
        p.startedAt = nowIso8601();
        return p;
    }
    return json::parse(readFile(file)).get<LabProgress>();
}

void LabProgress::save() const { writeFileAtomic(progressDir() / (labId + ".json"), json(*this).dump(2)); }

int LabProgress::totalPoints() const {
    int sum = 0;
    for (auto& [id, t] : tasks) sum += t.pointsEarned;
    return sum;
}

std::string renderReport(const Lab& lab, const LabProgress& progress, const std::string& studentName) {
    int maxPoints = 0;
    int done = 0, hints = 0, ai = 0;
    for (auto& t : lab.tasks) maxPoints += t.points;

    std::ostringstream md;
    md << "# Lab report: " << lab.title << "\n\n";
    md << "| | |\n|---|---|\n";
    md << "| Student | " << (studentName.empty() ? "(not set: eduvm config set studentName \"Your Name\")" : studentName) << " |\n";
    md << "| Lab | `" << lab.id << "` v" << lab.version << " |\n";
    md << "| Started | " << progress.startedAt << " |\n";
    md << "| Generated | " << nowIso8601() << " |\n\n";

    md << "## Tasks\n\n| Task | Status | Points | Attempts | Hints | AI questions |\n|---|---|---|---|---|---|\n";
    for (auto& task : lab.tasks) {
        TaskProgress tp;
        if (auto it = progress.tasks.find(task.id); it != progress.tasks.end()) tp = it->second;
        done += tp.completed ? 1 : 0;
        hints += tp.hintsRevealed;
        ai += tp.aiQuestions;
        md << "| " << task.title << " | " << (tp.completed ? "Done" : "Open") << " | " << tp.pointsEarned << "/"
           << task.points << " | " << tp.attempts << " | " << tp.hintsRevealed << " | " << tp.aiQuestions << " |\n";
    }
    md << "\n**Score: " << progress.totalPoints() << " / " << maxPoints << "**  \n";
    md << "Completed " << done << " of " << lab.tasks.size() << " tasks, using " << hints << " hint(s) and " << ai
       << " AI tutor question(s).\n\n";
    md << "_Results were verified automatically by EduVM inside the lab VMs._\n";
    return md.str();
}

}  // namespace eduvm
