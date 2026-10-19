#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace eduvm {

// One automated check, executed inside a lab VM through the guest agent.
struct LabCheck {
    std::string vm;           // lab VM name; may be omitted when the lab has one VM
    std::string description;
    std::string command;      // run with /bin/sh -c (Linux) or PowerShell (Windows)
    std::optional<int> expectExitCode;
    std::optional<std::string> expectOutput;    // exact match after trimming
    std::optional<std::string> expectContains;
    std::optional<std::string> expectRegex;
    int timeoutSeconds = 30;
};

struct LabTask {
    std::string id;
    std::string title;
    std::string instructions;
    int points = 10;
    std::vector<std::string> hints;  // instructor-written, revealed one by one
    std::vector<LabCheck> checks;
};

struct LabVm {
    std::string name;
    std::string guestOs = "linux";
    int cpus = 1;
    int memoryMb = 1024;
    int diskGb = 10;
    std::string baseImage;  // qcow2 file name in the images folder (or absolute path)
    std::string imageUrl;   // where to download baseImage from if it is missing
    std::string iso;        // alternative: blank disk + installer ISO
    std::string display = "embedded";
    std::vector<std::string> segments;
};

// The instructor decides how much AI help a lab allows.
struct LabAiPolicy {
    bool enabled = true;
    std::string mode;  // "" = student's choice, "coach" or "explain" = locked
    int maxQuestionsPerTask = 0;  // 0 = unlimited
    bool examMode = false;        // exam: AI off, hints off
};

struct Lab {
    std::string id;
    std::string title;
    std::string version = "1.0.0";
    std::string level;  // beginner | intermediate | advanced
    int estimatedMinutes = 0;
    std::string description;
    std::vector<std::string> objectives;
    LabAiPolicy ai;
    std::vector<LabVm> vms;
    std::vector<LabTask> tasks;

    const LabTask* findTask(const std::string& taskId) const;
    const LabVm* findVm(const std::string& vmName) const;
    // Name of the EduVM VM that backs a lab VM, e.g. "linux-basics-01--server".
    std::string instanceName(const LabVm& vm) const;
    // Segments are namespaced per lab so two labs never share a virtual switch.
    std::string segmentName(const std::string& segment) const;

    static Lab parse(const std::string& jsonText);
    static Lab loadFile(const std::filesystem::path& file);
};

void from_json(const nlohmann::json& j, Lab& lab);

// Installed labs live in labsDir()/<id>.json.
class LabLibrary {
public:
    explicit LabLibrary(std::filesystem::path dir);
    std::vector<Lab> list() const;
    Lab load(const std::string& id) const;
    // Accepts a local file path or an http(s) URL. Returns the installed lab.
    Lab install(const std::string& source) const;

private:
    std::filesystem::path dir_;
};

}  // namespace eduvm
