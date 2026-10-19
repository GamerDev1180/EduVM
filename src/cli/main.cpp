// eduvm: command-line front end for EduVM.
//
// The CLI is deliberately thin: every feature lives in eduvm_core so that the
// planned desktop GUI can reuse it unchanged.

#include <algorithm>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "core/AiTutor.hpp"
#include "core/Grader.hpp"
#include "core/HostInfo.hpp"
#include "core/Lab.hpp"
#include "core/LabRunner.hpp"
#include "core/Paths.hpp"
#include "core/Progress.hpp"
#include "core/QemuBackend.hpp"
#include "core/Settings.hpp"
#include "core/Strings.hpp"
#include "core/Updater.hpp"
#include "core/VmRepository.hpp"
#include "eduvm/Version.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

using namespace eduvm;
namespace fs = std::filesystem;

namespace {

// ---------------------------------------------------------------- arguments

struct Args {
    std::vector<std::string> positional;
    std::map<std::string, std::string> options;  // --key value
    std::set<std::string> flags;                 // --switch

    static Args parse(const std::vector<std::string>& raw, const std::set<std::string>& knownFlags) {
        Args a;
        for (size_t i = 0; i < raw.size(); ++i) {
            const std::string& s = raw[i];
            if (startsWith(s, "--")) {
                std::string key = s.substr(2);
                if (auto eq = key.find('='); eq != std::string::npos) {
                    a.options[key.substr(0, eq)] = key.substr(eq + 1);
                } else if (knownFlags.count(key)) {
                    a.flags.insert(key);
                } else if (i + 1 < raw.size()) {
                    a.options[key] = raw[++i];
                } else {
                    throw Error("Option --" + key + " needs a value");
                }
            } else {
                a.positional.push_back(s);
            }
        }
        return a;
    }

    const std::string& pos(size_t i, const char* what) const {
        if (i >= positional.size()) throw Error(std::string("Missing argument: ") + what);
        return positional[i];
    }
    std::string opt(const std::string& key, const std::string& fallback = {}) const {
        auto it = options.find(key);
        return it == options.end() ? fallback : it->second;
    }
    int intOpt(const std::string& key, int fallback) const {
        auto it = options.find(key);
        if (it == options.end()) return fallback;
        try {
            return std::stoi(it->second);
        } catch (const std::exception&) {
            throw Error("--" + key + " expects a number");
        }
    }
    bool flag(const std::string& key) const { return flags.count(key) > 0; }
};

// ------------------------------------------------------------------ context

struct App {
    Settings settings = Settings::load();
    VmRepository repo{vmsDir()};
    HostInfo host = HostInfo::detect();
    QemuBackend backend{settings, repo, host};
    LabLibrary labs{labsDir()};
};

void printWarnings(const std::vector<std::string>& warnings) {
    for (auto& w : warnings) std::cout << "  ! " << w << "\n";
}

std::string pad(const std::string& s, size_t width) { return s.size() >= width ? s + " " : s + std::string(width - s.size(), ' '); }

void printDownloadProgress(std::uint64_t done, std::uint64_t total) {
    static int lastPct = -1;
    if (total == 0) return;
    int pct = static_cast<int>(done * 100 / total);
    if (pct == lastPct) return;
    lastPct = pct;
    std::cout << "\r  downloading base image... " << pct << "% (" << done / (1024 * 1024) << " / " << total / (1024 * 1024)
              << " MB)" << std::flush;
    if (pct == 100) std::cout << "\n";
}

// --------------------------------------------------------------------- help

void printHelp() {
    std::cout << kProductName << " " << kVersion << R"( - virtual machine labs for ICT students

Usage: eduvm <command> [arguments]

Getting started
  doctor                              Check that this computer is ready to run VMs
  config get|set|list [key] [value]   View or change settings

Virtual machines
  vm create <name> [--os linux|windows|other] [--cpus N] [--memory MB]
                   [--disk GB] [--iso path] [--headless]
  vm list                             List VMs and their state
  vm info <name>                      Show details, ports and snapshots
  vm start|stop|pause|resume <name>   Control a VM (stop accepts --force)
  vm iso <name> <path|--eject>        Insert or eject installation media
  vm delete <name>                    Delete a VM and its disk

Snapshots
  snap create|restore|delete <vm> <snapshot>
  snap list <vm>

Labs
  lab list                            Installed labs
  lab install <file|url>              Install a lab manifest
  lab catalog                         Labs published by your instructor (marks updates)
  lab update                          Install all newer lab versions from the catalog
  lab start <lab>                     Create and boot the lab VMs
  lab status <lab>                    Tasks, score and VM state
  lab task <lab> <task>               Show the instructions for one task
  lab check <lab> [task]              Auto-grade your work inside the VMs
  lab hint <lab> <task>               Reveal the next instructor hint
  lab reset <lab> [--vm name] [--progress]   Restore VMs to the pristine image
  lab stop <lab> [--force]

AI tutor
  ai ask "<question>" [--lab id] [--task id] [--file output.txt]
  ai explain "<error message>" | --file log.txt
  ai reset [--lab id --task id]       Start a fresh conversation

Other
  report <lab> [--out report.md]      Generate a hand-in report
  update                              Check for a new EduVM version
  version
)";
}

// ------------------------------------------------------------------- doctor

int cmdDoctor(App& app) {
    int problems = 0;
    auto line = [&](bool ok, const std::string& what, const std::string& detail) {
        std::cout << (ok ? "  [ OK ] " : "  [FAIL] ") << pad(what, 22) << detail << "\n";
        if (!ok) ++problems;
    };
    std::cout << "EduVM " << kVersion << " system check\n\n";

    auto qemu = app.backend.qemuSystemBinary("x86_64");
    line(qemu.has_value(), "QEMU",
         qemu ? pathToUtf8(*qemu) : "not found: install from https://www.qemu.org/download/ (Windows: 'winget install SoftwareFreedomConservancy.QEMU')");
    auto img = app.backend.qemuImgBinary();
    line(img.has_value(), "qemu-img", img ? pathToUtf8(*img) : "not found (ships with QEMU)");

    line(app.host.accelerator != Accelerator::None, "Acceleration",
         toString(app.host.accelerator) + " - " + app.host.acceleratorDetail);
    line(app.host.totalRamMb >= 8192, "Memory",
         std::to_string(app.host.totalRamMb) + " MB total, " + std::to_string(app.host.availableRamMb) + " MB free" +
             (app.host.totalRamMb < 8192 ? " (8 GB+ recommended)" : ""));
    line(app.host.logicalCpus >= 4, "CPU", std::to_string(app.host.logicalCpus) + " logical cores");
    line(app.host.freeDiskMb >= 20480, "Disk space", std::to_string(app.host.freeDiskMb / 1024) + " GB free in " + pathToUtf8(dataDir()));

    if (app.settings.aiEnabled) {
        bool proxy = app.settings.apiKeyEnv.empty();
        bool key = proxy || getEnv(app.settings.apiKeyEnv.c_str()).has_value();
        line(key, "AI tutor",
             proxy ? "using school proxy " + app.settings.aiBaseUrl
                   : (key ? "API key found in " + app.settings.apiKeyEnv
                          : "set the " + app.settings.apiKeyEnv + " environment variable (ask your instructor)"));
    } else {
        std::cout << "  [ -- ] " << pad("AI tutor", 22) << "disabled in settings\n";
    }

    std::cout << "\nData folder: " << pathToUtf8(dataDir()) << "\n";
    std::cout << (problems == 0 ? "\nAll good. Try 'eduvm lab list' to find a lab.\n"
                                : "\n" + std::to_string(problems) + " problem(s) found. Fix the [FAIL] lines above.\n");
    return problems == 0 ? 0 : 1;
}

// ------------------------------------------------------------------- config

int cmdConfig(App& app, const Args& a) {
    const std::string& sub = a.pos(0, "get|set|list");
    if (sub == "list") {
        for (auto& k : Settings::keys()) std::cout << pad(k, 16) << app.settings.get(k) << "\n";
        std::cout << "\n(file: " << pathToUtf8(settingsFile()) << ")\n";
    } else if (sub == "get") {
        std::cout << app.settings.get(a.pos(1, "key")) << "\n";
    } else if (sub == "set") {
        app.settings.set(a.pos(1, "key"), a.pos(2, "value"));
        app.settings.save();
        std::cout << "Saved.\n";
    } else {
        throw Error("Unknown config command '" + sub + "'");
    }
    return 0;
}

// ----------------------------------------------------------------------- vm

int cmdVm(App& app, const Args& a) {
    const std::string& sub = a.pos(0, "vm command");

    if (sub == "list") {
        auto vms = app.repo.list();
        if (vms.empty()) {
            std::cout << "No VMs yet. Create one with 'eduvm vm create <name> --iso <installer.iso>' or start a lab.\n";
            return 0;
        }
        std::cout << pad("NAME", 34) << pad("STATE", 10) << pad("OS", 9) << pad("CPU", 5) << pad("RAM", 8) << "LAB\n";
        for (auto& vm : vms) {
            std::cout << pad(vm.name, 34) << pad(toString(app.backend.state(vm.name)), 10) << pad(toString(vm.guestOs), 9)
                      << pad(std::to_string(vm.cpus), 5) << pad(std::to_string(vm.memoryMb) + "M", 8) << vm.labId << "\n";
        }
        return 0;
    }

    const std::string& name = a.pos(1, "VM name");

    if (sub == "create") {
        if (!isValidVmName(name)) throw Error("VM names may contain letters, digits, - and _ (max 64)");
        if (app.repo.exists(name)) throw Error("A VM named '" + name + "' already exists");

        VmConfig cfg;
        cfg.name = name;
        cfg.guestOs = guestOsFromString(a.opt("os", "linux"));
        auto advice = recommendResources(app.host, a.intOpt("cpus", 2),
                                         a.intOpt("memory", cfg.guestOs == GuestOs::Windows ? 4096 : 2048));
        cfg.cpus = advice.cpus;
        cfg.memoryMb = advice.memoryMb;
        cfg.diskGb = a.intOpt("disk", cfg.guestOs == GuestOs::Windows ? 64 : 20);
        if (a.flag("headless")) cfg.display = DisplayMode::Headless;
        if (auto iso = a.opt("iso"); !iso.empty()) {
            if (!fs::exists(utf8ToPath(iso))) throw Error("ISO not found: " + iso);
            cfg.isoPath = pathToUtf8(fs::absolute(utf8ToPath(iso)));
        }
        cfg.createdAt = nowIso8601();

        app.repo.save(cfg);
        try {
            app.backend.createDisk(app.repo.diskPath(cfg), cfg.diskGb);
        } catch (...) {
            app.repo.remove(name);
            throw;
        }
        printWarnings(advice.warnings);
        std::cout << "Created VM '" << name << "' (" << cfg.cpus << " CPU, " << cfg.memoryMb << " MB RAM, " << cfg.diskGb
                  << " GB disk).\nStart it with: eduvm vm start " << name << "\n";
        return 0;
    }
    if (sub == "info") {
        VmConfig cfg = app.repo.load(name);
        std::cout << "Name:      " << cfg.name << "\n"
                  << "State:     " << toString(app.backend.state(name)) << "\n"
                  << "Guest OS:  " << toString(cfg.guestOs) << " (" << cfg.arch << ")\n"
                  << "Resources: " << cfg.cpus << " CPU, " << cfg.memoryMb << " MB RAM, " << cfg.diskGb << " GB disk\n"
                  << "Disk:      " << pathToUtf8(app.repo.diskPath(cfg)) << "\n";
        if (!cfg.isoPath.empty()) std::cout << "ISO:       " << cfg.isoPath << "\n";
        if (!cfg.segments.empty()) std::cout << "Networks:  " << join(cfg.segments, ", ") << "\n";
        if (!cfg.labId.empty()) std::cout << "Lab:       " << cfg.labId << "\n";
        if (auto rt = app.repo.loadRuntime(name); rt && rt->sshPort)
            std::cout << "SSH:       ssh -p " << rt->sshPort << " <user>@127.0.0.1\n";
        try {
            auto snaps = app.backend.listSnapshots(name);
            std::cout << "Snapshots: " << (snaps.empty() ? "none" : "") << "\n";
            for (auto& s : snaps) std::cout << "  - " << pad(s.name, 24) << s.date << (s.includesRam ? "  (live)" : "") << "\n";
        } catch (const Error&) {
        }
        return 0;
    }
    if (sub == "start") {
        VmConfig cfg = app.repo.load(name);
        printWarnings(recommendResources(app.host, cfg.cpus, cfg.memoryMb).warnings);
        VmRuntime rt = app.backend.start(cfg);
        std::cout << "Started '" << name << "'.";
        if (rt.sshPort) std::cout << " SSH: ssh -p " << rt.sshPort << " <user>@127.0.0.1";
        std::cout << "\n";
        return 0;
    }
    if (sub == "stop") {
        std::cout << (a.flag("force") ? "Powering off '" : "Asking '") << name
                  << (a.flag("force") ? "'...\n" : "' to shut down (up to 90s)...\n");
        app.backend.stop(name, a.flag("force"));
        std::cout << "Stopped.\n";
        return 0;
    }
    if (sub == "pause") {
        app.backend.pause(name);
        std::cout << "Paused.\n";
        return 0;
    }
    if (sub == "resume") {
        app.backend.resume(name);
        std::cout << "Resumed.\n";
        return 0;
    }
    if (sub == "iso") {
        VmConfig cfg = app.repo.load(name);
        if (a.flag("eject")) {
            cfg.isoPath.clear();
        } else {
            const std::string& iso = a.pos(2, "ISO path");
            if (!fs::exists(utf8ToPath(iso))) throw Error("ISO not found: " + iso);
            cfg.isoPath = pathToUtf8(fs::absolute(utf8ToPath(iso)));
        }
        app.repo.save(cfg);
        std::cout << "Saved. The change applies the next time the VM starts.\n";
        return 0;
    }
    if (sub == "delete") {
        if (app.backend.state(name) != VmState::Stopped) throw Error("Stop the VM first: eduvm vm stop " + name);
        app.repo.load(name);  // existence check with a friendly message
        app.repo.remove(name);
        std::cout << "Deleted '" << name << "'.\n";
        return 0;
    }
    throw Error("Unknown vm command '" + sub + "'. Run 'eduvm help'.");
}

// --------------------------------------------------------------------- snap

int cmdSnap(App& app, const Args& a) {
    const std::string& sub = a.pos(0, "snap command");
    const std::string& vm = a.pos(1, "VM name");
    if (sub == "list") {
        auto snaps = app.backend.listSnapshots(vm);
        if (snaps.empty()) std::cout << "No snapshots. Tip: take one before risky changes: eduvm snap create " << vm << " before-change\n";
        for (auto& s : snaps) std::cout << pad(s.name, 24) << pad(s.date, 18) << (s.includesRam ? "live (RAM included)" : "disk only") << "\n";
        return 0;
    }
    const std::string& snap = a.pos(2, "snapshot name");
    if (sub == "create") app.backend.createSnapshot(vm, snap);
    else if (sub == "restore") app.backend.restoreSnapshot(vm, snap);
    else if (sub == "delete") app.backend.deleteSnapshot(vm, snap);
    else throw Error("Unknown snap command '" + sub + "'");
    std::cout << "Done.\n";
    return 0;
}

// ---------------------------------------------------------------------- lab

const LabTask& requireTask(const Lab& lab, const std::string& id) {
    const LabTask* t = lab.findTask(id);
    if (!t) throw Error("Lab '" + lab.id + "' has no task '" + id + "'. Run 'eduvm lab status " + lab.id + "'.");
    return *t;
}

const LabTask* firstOpenTask(const Lab& lab, const LabProgress& progress) {
    for (auto& t : lab.tasks) {
        auto it = progress.tasks.find(t.id);
        if (it == progress.tasks.end() || !it->second.completed) return &t;
    }
    return nullptr;
}

void printTask(const LabTask& t) {
    std::cout << "\n== " << t.title << "  [" << t.id << ", " << t.points << " pts]\n\n" << t.instructions << "\n";
}

std::string formatFeedback(const TaskResult& r) {
    std::string s;
    for (auto& c : r.checks)
        if (!c.passed) s += "- " + c.description + ": " + c.detail + "\n";
    return s;
}

int cmdLab(App& app, const Args& a) {
    const std::string& sub = a.pos(0, "lab command");

    if (sub == "list") {
        auto labs = app.labs.list();
        if (labs.empty()) {
            std::cout << "No labs installed. Install one with 'eduvm lab install <file|url>' or see 'eduvm lab catalog'.\n";
            return 0;
        }
        for (auto& lab : labs) {
            auto progress = LabProgress::load(lab.id);
            int done = 0;
            for (auto& t : lab.tasks) done += progress.task(t.id).completed ? 1 : 0;
            std::cout << pad(lab.id, 28) << pad(lab.level, 14) << pad(std::to_string(done) + "/" + std::to_string(lab.tasks.size()), 7)
                      << lab.title << "\n";
        }
        return 0;
    }
    if (sub == "install") {
        Lab lab = app.labs.install(a.pos(1, "file or URL"));
        std::cout << "Installed '" << lab.title << "' (" << lab.id << " v" << lab.version << ").\nStart it with: eduvm lab start "
                  << lab.id << "\n";
        return 0;
    }
    if (sub == "catalog" || sub == "update") {
        auto entries = fetchLabCatalog(app.settings.labCatalogUrl);
        std::map<std::string, std::string> installed;
        for (auto& lab : app.labs.list()) installed[lab.id] = lab.version;
        int updated = 0;
        for (auto& e : entries) {
            auto it = installed.find(e.id);
            bool newer = it != installed.end() && compareVersions(e.version, it->second) > 0;
            if (sub == "update") {
                if (newer) {
                    app.labs.install(e.url);
                    std::cout << "Updated " << e.id << " " << it->second << " -> " << e.version << "\n";
                    ++updated;
                }
                continue;
            }
            std::string state = it == installed.end() ? "available" : newer ? "update " + it->second + " -> " + e.version : "installed";
            std::cout << pad(e.id, 28) << pad(e.level, 14) << pad(state, 22) << e.title << "\n";
        }
        if (sub == "update") std::cout << (updated ? std::to_string(updated) + " lab(s) updated.\n" : "All labs are up to date.\n");
        return 0;
    }

    Lab lab = app.labs.load(a.pos(1, "lab id"));
    LabRunner runner(app.backend, app.repo, app.host, lab);
    LabProgress progress = LabProgress::load(lab.id);
    progress.labVersion = lab.version;

    if (sub == "start") {
        std::cout << "Preparing '" << lab.title << "'...\n";
        printWarnings(runner.provision(printDownloadProgress));
        runner.startAll();
        progress.save();
        std::cout << "Lab VMs are running:\n";
        for (auto& vm : lab.vms) {
            std::string instance = lab.instanceName(vm);
            std::cout << "  - " << pad(vm.name, 16);
            if (auto rt = app.repo.loadRuntime(instance); rt && rt->sshPort) std::cout << "ssh -p " << rt->sshPort << " <user>@127.0.0.1";
            std::cout << "\n";
        }
        if (const LabTask* t = firstOpenTask(lab, progress)) {
            printTask(*t);
            std::cout << "\nWhen you think you are done: eduvm lab check " << lab.id << " " << t->id << "\n";
        } else {
            std::cout << "\nAll tasks are already complete. Use 'eduvm lab reset " << lab.id << " --progress' to redo the lab.\n";
        }
        return 0;
    }
    if (sub == "status") {
        std::cout << lab.title << " (v" << lab.version << ")\n\nVMs:\n";
        for (auto& [vm, state] : runner.status())
            std::cout << "  " << pad(vm, 16) << (state == VmState::Unknown ? "not created" : toString(state)) << "\n";
        int max = 0;
        std::cout << "\nTasks:\n";
        for (auto& t : lab.tasks) {
            const TaskProgress& tp = progress.task(t.id);
            max += t.points;
            std::cout << "  [" << (tp.completed ? "x" : " ") << "] " << pad(t.id, 20) << pad(std::to_string(tp.pointsEarned) + "/" + std::to_string(t.points), 8)
                      << t.title << "\n";
        }
        std::cout << "\nScore: " << progress.totalPoints() << " / " << max << "\n";
        return 0;
    }
    if (sub == "task") {
        printTask(requireTask(lab, a.pos(2, "task id")));
        return 0;
    }
    if (sub == "check") {
        std::vector<const LabTask*> toCheck;
        if (a.positional.size() > 2) toCheck.push_back(&requireTask(lab, a.positional[2]));
        else
            for (auto& t : lab.tasks) toCheck.push_back(&t);

        // A stopped VM is not a failed attempt; don't let it count against the student.
        for (auto& [vm, state] : runner.status()) {
            if (state != VmState::Running) throw Error("Lab VM '" + vm + "' is not running. Start the lab first: eduvm lab start " + lab.id);
        }

        Grader grader(app.backend, lab);
        for (const LabTask* t : toCheck) {
            TaskResult r = grader.grade(*t);
            TaskProgress& tp = progress.task(t->id);
            ++tp.attempts;
            tp.lastFeedback = formatFeedback(r);
            if (r.passed && !tp.completed) {
                tp.completed = true;
                tp.completedAt = nowIso8601();
            }
            tp.pointsEarned = std::max(tp.pointsEarned, r.points);

            std::cout << (r.passed ? "[PASS] " : "[    ] ") << t->title << "\n";
            for (auto& c : r.checks) std::cout << "       " << (c.passed ? "ok   " : "FAIL ") << c.description << (c.passed ? "" : "\n             " + c.detail) << "\n";
        }
        progress.save();

        if (const LabTask* next = firstOpenTask(lab, progress)) {
            std::cout << "\nNext open task: " << next->id << ". Stuck? Try 'eduvm lab hint " << lab.id << " " << next->id
                      << "' or 'eduvm ai ask \"...\" --lab " << lab.id << "'.\n";
        } else {
            std::cout << "\nAll tasks complete! Create your report with: eduvm report " << lab.id << "\n";
        }
        return 0;
    }
    if (sub == "hint") {
        if (lab.ai.examMode) throw Error("Hints are disabled: this lab is in exam mode.");
        const LabTask& t = requireTask(lab, a.pos(2, "task id"));
        TaskProgress& tp = progress.task(t.id);
        if (t.hints.empty()) {
            std::cout << "This task has no instructor hints. Ask the AI tutor: eduvm ai ask \"...\" --lab " << lab.id << " --task " << t.id << "\n";
            return 0;
        }
        if (tp.hintsRevealed < static_cast<int>(t.hints.size())) ++tp.hintsRevealed;
        progress.save();
        for (int i = 0; i < tp.hintsRevealed; ++i) std::cout << "Hint " << i + 1 << "/" << t.hints.size() << ": " << t.hints[i] << "\n";
        if (tp.hintsRevealed == static_cast<int>(t.hints.size())) std::cout << "(That was the last hint.)\n";
        return 0;
    }
    if (sub == "reset") {
        runner.reset(a.opt("vm"));
        if (a.flag("progress")) {
            progress = LabProgress{};
            progress.labId = lab.id;
            progress.startedAt = nowIso8601();
            progress.save();
        }
        std::cout << "Lab VMs restored to their original state" << (a.flag("progress") ? " and progress cleared" : "")
                  << ". Start again with: eduvm lab start " << lab.id << "\n";
        return 0;
    }
    if (sub == "stop") {
        runner.stopAll(a.flag("force"));
        std::cout << "Lab VMs stopped. Your work is kept on disk.\n";
        return 0;
    }
    throw Error("Unknown lab command '" + sub + "'. Run 'eduvm help'.");
}

// ----------------------------------------------------------------------- ai

int cmdAi(App& app, const Args& a) {
    const std::string& sub = a.pos(0, "ask|explain|reset");

    std::optional<Lab> lab;
    if (auto id = a.opt("lab"); !id.empty()) lab = app.labs.load(id);
    std::optional<LabProgress> progress;
    const LabTask* task = nullptr;
    if (lab) {
        progress = LabProgress::load(lab->id);
        task = a.opt("task").empty() ? firstOpenTask(*lab, *progress) : &requireTask(*lab, a.opt("task"));
    }
    std::string key = lab ? lab->id + "_" + (task ? task->id : "general") : "general";

    if (sub == "reset") {
        TutorConversation::load(key).clear();
        std::cout << "Started a fresh tutor conversation.\n";
        return 0;
    }

    TutorContext ctx;
    ctx.lab = lab ? &*lab : nullptr;
    ctx.task = task;
    if (auto file = a.opt("file"); !file.empty()) {
        ctx.attachedOutput = readFile(utf8ToPath(file));
        if (ctx.attachedOutput.size() > 20000) ctx.attachedOutput = "...(earlier output omitted)...\n" + ctx.attachedOutput.substr(ctx.attachedOutput.size() - 20000);
    }

    std::string question;
    if (sub == "ask") {
        question = a.pos(1, "question (in quotes)");
    } else if (sub == "explain") {
        if (a.positional.size() > 1) ctx.attachedOutput = a.positional[1];
        if (ctx.attachedOutput.empty()) throw Error("Give the error text in quotes or use --file <log>");
        question = "Can you explain what this output or error means and what I should look at?";
    } else {
        throw Error("Unknown ai command '" + sub + "'");
    }

    if (task && progress) {
        TaskProgress& tp = progress->task(task->id);
        int limit = lab->ai.maxQuestionsPerTask;
        if (limit > 0 && tp.aiQuestions >= limit)
            throw Error("You have used all " + std::to_string(limit) + " AI questions for this task. Try the hints or ask your instructor.");
        ctx.hintsRevealed = tp.hintsRevealed;
        ctx.gradingFeedback = tp.lastFeedback;
    }

    AiTutor tutor(app.settings);
    TutorConversation conversation = TutorConversation::load(key);
    TutorMode mode = AiTutor::effectiveMode(app.settings, ctx.lab ? &ctx.lab->ai : nullptr);
    std::cout << "(tutor: " << (mode == TutorMode::Coach ? "coach mode" : "explain mode")
              << (task ? ", task " + task->id : std::string()) << ")\n\n" << std::flush;

    TutorReply reply = tutor.ask(question, ctx, conversation);
    std::cout << trim(reply.text) << "\n";
    if (reply.truncated) std::cout << "\n(The answer was cut off. Ask a narrower follow-up question.)\n";

    if (task && progress && !reply.refused) {
        ++progress->task(task->id).aiQuestions;
        progress->save();
    }
    return 0;
}

// ------------------------------------------------------------------- report

int cmdReport(App& app, const Args& a) {
    Lab lab = app.labs.load(a.pos(0, "lab id"));
    std::string md = renderReport(lab, LabProgress::load(lab.id), app.settings.studentName);
    if (auto out = a.opt("out"); !out.empty()) {
        writeFileAtomic(utf8ToPath(out), md);
        std::cout << "Report written to " << out << "\n";
    } else {
        std::cout << md;
    }
    return 0;
}

int cmdUpdate(App& app) {
    UpdateInfo info = checkForAppUpdate(app.settings);
    if (!info.available) {
        std::cout << "EduVM " << info.currentVersion << " is up to date.\n";
        return 0;
    }
    std::cout << "EduVM " << info.latestVersion << " is available (you have " << info.currentVersion << ").\n"
              << "Download: " << info.downloadUrl << "\n";
    if (!info.releaseNotes.empty()) std::cout << "\nWhat's new:\n" << info.releaseNotes << "\n";
    return 0;
}

int run(const std::vector<std::string>& argv) {
    if (argv.empty() || argv[0] == "help" || argv[0] == "--help" || argv[0] == "-h") {
        printHelp();
        return 0;
    }
    if (argv[0] == "version" || argv[0] == "--version") {
        std::cout << kProductName << " " << kVersion << "\n";
        return 0;
    }

    const std::string cmd = argv[0];
    Args args = Args::parse({argv.begin() + 1, argv.end()}, {"force", "headless", "eject", "progress"});
    App app;

    if (cmd == "doctor") return cmdDoctor(app);
    if (cmd == "config") return cmdConfig(app, args);
    if (cmd == "vm") return cmdVm(app, args);
    if (cmd == "snap") return cmdSnap(app, args);
    if (cmd == "lab") return cmdLab(app, args);
    if (cmd == "ai") return cmdAi(app, args);
    if (cmd == "report") return cmdReport(app, args);
    if (cmd == "update") return cmdUpdate(app);
    throw Error("Unknown command '" + cmd + "'. Run 'eduvm help'.");
}

}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** wargv) {
    SetConsoleOutputCP(CP_UTF8);  // AI answers and lab text are UTF-8
    std::vector<std::string> argv;
    for (int i = 1; i < argc; ++i) argv.push_back(narrow(wargv[i]));
#else
int main(int argc, char** cargv) {
    std::vector<std::string> argv(cargv + 1, cargv + argc);
#endif
    try {
        return run(argv);
    } catch (const Error& e) {
        std::cerr << "error: " << e.what() << "\n";
    } catch (const std::exception& e) {
        std::cerr << "unexpected error: " << e.what() << "\n";
    }
    return 1;
}
