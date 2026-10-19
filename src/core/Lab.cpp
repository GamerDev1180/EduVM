#include "core/Lab.hpp"

#include <algorithm>
#include <set>

#include "core/Http.hpp"
#include "core/Paths.hpp"
#include "core/Strings.hpp"
#include "core/VmConfig.hpp"

namespace fs = std::filesystem;
using nlohmann::json;

namespace eduvm {

template <typename T>
static std::optional<T> optionalField(const json& j, const char* key) {
    if (!j.contains(key) || j[key].is_null()) return std::nullopt;
    return j[key].get<T>();
}

static LabCheck parseCheck(const json& j) {
    LabCheck c;
    c.vm = j.value("vm", std::string());
    c.description = j.value("description", std::string());
    c.command = j.at("command").get<std::string>();
    c.expectExitCode = optionalField<int>(j, "expectExitCode");
    c.expectOutput = optionalField<std::string>(j, "expectOutput");
    c.expectContains = optionalField<std::string>(j, "expectContains");
    c.expectRegex = optionalField<std::string>(j, "expectRegex");
    c.timeoutSeconds = j.value("timeoutSeconds", 30);
    // A check with no expectation would always pass; default to "exit 0".
    if (!c.expectExitCode && !c.expectOutput && !c.expectContains && !c.expectRegex) c.expectExitCode = 0;
    if (c.description.empty()) c.description = c.command;
    return c;
}

void from_json(const json& j, Lab& lab) {
    lab.id = j.at("id").get<std::string>();
    lab.title = j.at("title").get<std::string>();
    lab.version = j.value("version", lab.version);
    lab.level = j.value("level", std::string());
    lab.estimatedMinutes = j.value("estimatedMinutes", 0);
    lab.description = j.value("description", std::string());
    lab.objectives = j.value("objectives", std::vector<std::string>{});

    if (j.contains("ai")) {
        const json& a = j["ai"];
        lab.ai.enabled = a.value("enabled", true);
        lab.ai.mode = a.value("mode", std::string());
        lab.ai.maxQuestionsPerTask = a.value("maxQuestionsPerTask", 0);
        lab.ai.examMode = a.value("examMode", false);
    }

    for (const json& v : j.value("vms", json::array())) {
        LabVm vm;
        vm.name = v.at("name").get<std::string>();
        vm.guestOs = v.value("guestOs", vm.guestOs);
        vm.cpus = v.value("cpus", vm.cpus);
        vm.memoryMb = v.value("memoryMb", vm.memoryMb);
        vm.diskGb = v.value("diskGb", vm.diskGb);
        vm.baseImage = v.value("baseImage", std::string());
        vm.imageUrl = v.value("imageUrl", std::string());
        vm.iso = v.value("iso", std::string());
        vm.display = v.value("display", vm.display);
        vm.segments = v.value("segments", std::vector<std::string>{});
        lab.vms.push_back(vm);
    }

    for (const json& t : j.value("tasks", json::array())) {
        LabTask task;
        task.id = t.at("id").get<std::string>();
        task.title = t.at("title").get<std::string>();
        task.instructions = t.value("instructions", std::string());
        task.points = t.value("points", 10);
        task.hints = t.value("hints", std::vector<std::string>{});
        for (const json& c : t.value("checks", json::array())) task.checks.push_back(parseCheck(c));
        lab.tasks.push_back(task);
    }
}

static void validate(Lab& lab) {
    if (!isValidVmName(lab.id)) throw Error("Lab id '" + lab.id + "' may only contain letters, digits, - and _");
    if (lab.vms.empty()) throw Error("Lab '" + lab.id + "' defines no VMs");

    std::set<std::string> vmNames;
    for (auto& vm : lab.vms) {
        if (!isValidVmName(vm.name)) throw Error("Lab VM name '" + vm.name + "' is invalid");
        if (!vmNames.insert(vm.name).second) throw Error("Duplicate lab VM '" + vm.name + "'");
        if (lab.instanceName(vm).size() > 64) throw Error("Lab id + VM name too long for '" + vm.name + "'");
        guestOsFromString(vm.guestOs);  // throws on typos
    }

    std::set<std::string> taskIds;
    for (auto& task : lab.tasks) {
        if (!taskIds.insert(task.id).second) throw Error("Duplicate task id '" + task.id + "'");
        for (auto& check : task.checks) {
            if (check.vm.empty()) {
                if (lab.vms.size() != 1) throw Error("Task '" + task.id + "': check must name its vm");
                check.vm = lab.vms.front().name;
            }
            if (!vmNames.count(check.vm)) throw Error("Task '" + task.id + "' checks unknown vm '" + check.vm + "'");
        }
    }
}

Lab Lab::parse(const std::string& jsonText) {
    json j = json::parse(jsonText, nullptr, false);
    if (j.is_discarded()) throw Error("Lab file is not valid JSON");
    Lab lab;
    try {
        lab = j.get<Lab>();
    } catch (const json::exception& e) {
        throw Error(std::string("Lab file is missing a required field: ") + e.what());
    }
    validate(lab);
    return lab;
}

Lab Lab::loadFile(const fs::path& file) { return parse(readFile(file)); }

const LabTask* Lab::findTask(const std::string& taskId) const {
    auto it = std::find_if(tasks.begin(), tasks.end(), [&](const LabTask& t) { return t.id == taskId; });
    return it == tasks.end() ? nullptr : &*it;
}

const LabVm* Lab::findVm(const std::string& vmName) const {
    auto it = std::find_if(vms.begin(), vms.end(), [&](const LabVm& v) { return v.name == vmName; });
    return it == vms.end() ? nullptr : &*it;
}

std::string Lab::instanceName(const LabVm& vm) const { return id + "--" + vm.name; }

std::string Lab::segmentName(const std::string& segment) const { return id + "/" + segment; }

LabLibrary::LabLibrary(fs::path dir) : dir_(std::move(dir)) {}

std::vector<Lab> LabLibrary::list() const {
    std::vector<Lab> labs;
    std::error_code ec;
    for (auto& entry : fs::directory_iterator(dir_, ec)) {
        if (entry.path().extension() != ".json") continue;
        try {
            labs.push_back(Lab::loadFile(entry.path()));
        } catch (const std::exception&) {
            // Skip broken manifests; `eduvm lab install` reports errors up front.
        }
    }
    std::sort(labs.begin(), labs.end(), [](const Lab& a, const Lab& b) { return a.id < b.id; });
    return labs;
}

Lab LabLibrary::load(const std::string& id) const {
    if (!isValidVmName(id)) throw Error("Invalid lab id '" + id + "'");
    fs::path file = dir_ / (id + ".json");
    if (!fs::exists(file)) throw Error("Lab '" + id + "' is not installed. Run 'eduvm lab list'.");
    return Lab::loadFile(file);
}

Lab LabLibrary::install(const std::string& source) const {
    std::string text;
    if (startsWith(source, "http://") || startsWith(source, "https://")) {
        HttpResponse r = httpGet(source);
        if (r.status != 200) throw Error("Download failed with HTTP " + std::to_string(r.status));
        text = r.body;
    } else {
        text = readFile(utf8ToPath(source));
    }
    Lab lab = Lab::parse(text);  // validate before writing anything
    writeFileAtomic(dir_ / (lab.id + ".json"), text);
    return lab;
}

}  // namespace eduvm
