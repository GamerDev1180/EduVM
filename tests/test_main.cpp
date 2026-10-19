// Dependency-free unit tests for the parts of EduVM that don't need QEMU or
// network access. Run: ctest --test-dir build -C Release

#include <algorithm>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include "core/AiTutor.hpp"
#include "core/Grader.hpp"
#include "core/Lab.hpp"
#include "core/Paths.hpp"
#include "core/Process.hpp"
#include "core/QemuBackend.hpp"
#include "core/Strings.hpp"
#include "core/Updater.hpp"

using namespace eduvm;

namespace {

int failures = 0;
std::vector<std::pair<std::string, std::function<void()>>>& registry() {
    static std::vector<std::pair<std::string, std::function<void()>>> r;
    return r;
}
struct Register {
    Register(const char* name, std::function<void()> fn) { registry().emplace_back(name, std::move(fn)); }
};

#define TEST(name)                                   \
    static void name();                              \
    static Register reg_##name(#name, name);         \
    static void name()

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::cerr << "  " << __FILE__ << ":" << __LINE__ << ": CHECK(" #cond ") failed\n"; \
            ++failures;                                                                   \
        }                                                                                 \
    } while (0)

#define CHECK_THROWS(expr)                  \
    do {                                    \
        bool threw = false;                 \
        try {                               \
            expr;                           \
        } catch (const std::exception&) {   \
            threw = true;                   \
        }                                   \
        CHECK(threw);                       \
    } while (0)

bool contains(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

}  // namespace

TEST(versions_compare_numerically) {
    CHECK(compareVersions("1.2.10", "1.2.9") > 0);
    CHECK(compareVersions("v1.0.0", "1.0") == 0);
    CHECK(compareVersions("0.9", "1.0.0") < 0);
    CHECK(compareVersions("2.0.0-beta", "2.0.0") == 0);
}

TEST(base64_decodes_guest_agent_output) {
    CHECK(base64Decode("aGVsbG8gd29ybGQK") == "hello world\n");
    CHECK(base64Decode("") == "");
}

TEST(vm_names_are_validated) {
    CHECK(isValidVmName("ubuntu-server_01"));
    CHECK(!isValidVmName(""));
    CHECK(!isValidVmName("../evil"));
    CHECK(!isValidVmName("has space"));
    CHECK(!isValidVmName(std::string(65, 'a')));
}

TEST(vm_config_roundtrips_through_json) {
    VmConfig c;
    c.name = "web1";
    c.guestOs = GuestOs::Windows;
    c.segments = {"lab/lan"};
    c.display = DisplayMode::Headless;
    VmConfig back = nlohmann::json(c).get<VmConfig>();
    CHECK(back.name == "web1");
    CHECK(back.guestOs == GuestOs::Windows);
    CHECK(back.display == DisplayMode::Headless);
    CHECK(back.segments.size() == 1 && back.segments[0] == "lab/lan");
}

TEST(qemu_args_use_acceleration_with_tcg_fallback) {
    QemuLaunchSpec spec;
    spec.qemuBinary = "qemu-system-x86_64";
    spec.diskPath = "disk.qcow2";
    spec.config.name = "srv";
    spec.config.segments = {"lab1/lan"};
    spec.runtime = {0, 4444, 5555, 2222, ""};
    spec.accelerator = Accelerator::Whpx;
    auto args = buildQemuArgs(spec);

    CHECK(contains(args, "whpx,kernel-irqchip=off"));
    CHECK(contains(args, "tcg"));
    CHECK(contains(args, "user,id=net0,hostfwd=tcp:127.0.0.1:2222-:22"));
    CHECK(contains(args, "tcp:127.0.0.1:4444,server=on,wait=off"));
    CHECK(contains(args, "file=disk.qcow2,format=qcow2,if=virtio"));
    bool hasSegment = std::any_of(args.begin(), args.end(), [](const std::string& s) { return startsWith(s, "socket,id=seg0,mcast=230.0.0.1:"); });
    CHECK(hasSegment);
}

TEST(windows_guests_get_compatible_devices) {
    QemuLaunchSpec spec;
    spec.qemuBinary = "qemu-system-x86_64";
    spec.diskPath = "disk.qcow2";
    spec.config.name = "win";
    spec.config.guestOs = GuestOs::Windows;
    auto args = buildQemuArgs(spec);
    CHECK(contains(args, "file=disk.qcow2,format=qcow2,if=ide"));
    CHECK(std::any_of(args.begin(), args.end(), [](const std::string& s) { return startsWith(s, "e1000,netdev=net0"); }));
    CHECK(contains(args, "base=localtime"));
}

TEST(segments_and_macs_are_deterministic) {
    CHECK(segmentMulticastPort("lab/lan") == segmentMulticastPort("lab/lan"));
    CHECK(segmentMulticastPort("lab/lan") >= 20000 && segmentMulticastPort("lab/lan") < 30000);
    CHECK(deterministicMac("a", 0) == deterministicMac("a", 0));
    CHECK(deterministicMac("a", 0) != deterministicMac("a", 1));
    CHECK(startsWith(deterministicMac("a", 0), "52:54:00:"));
}

#ifdef _WIN32
TEST(windows_command_line_quoting) {
    CHECK(buildWindowsCommandLine({"a", "b c"}) == "a \"b c\"");
    CHECK(buildWindowsCommandLine({"say \"hi\""}) == "\"say \\\"hi\\\"\"");
    CHECK(buildWindowsCommandLine({"C:\\dir with space\\"}) == "\"C:\\dir with space\\\\\"");
    CHECK(buildWindowsCommandLine({""}) == "\"\"");
}
#endif

const char* kLab = R"({
  "id": "t1", "title": "Test", "vms": [{"name": "srv"}],
  "ai": {"mode": "coach", "maxQuestionsPerTask": 2},
  "tasks": [{"id": "a", "title": "A", "checks": [{"command": "true"}]}]
})";

TEST(lab_parses_and_fills_defaults) {
    Lab lab = Lab::parse(kLab);
    CHECK(lab.id == "t1");
    CHECK(lab.tasks[0].checks[0].vm == "srv");          // single-VM labs may omit vm
    CHECK(lab.tasks[0].checks[0].expectExitCode == 0);  // default expectation
    CHECK(lab.instanceName(lab.vms[0]) == "t1--srv");
    CHECK(lab.segmentName("lan") == "t1/lan");
    CHECK(lab.ai.maxQuestionsPerTask == 2);
}

TEST(lab_validation_rejects_mistakes) {
    CHECK_THROWS(Lab::parse("not json"));
    CHECK_THROWS(Lab::parse(R"({"id":"x","title":"x","vms":[]})"));
    CHECK_THROWS(Lab::parse(R"({"id":"x","title":"x","vms":[{"name":"a"},{"name":"b"}],
                                "tasks":[{"id":"t","title":"t","checks":[{"command":"true"}]}]})"));
    CHECK_THROWS(Lab::parse(R"({"id":"x","title":"x","vms":[{"name":"a","guestOs":"dos"}]})"));
}

TEST(sample_labs_are_valid) {
    for (auto& entry : std::filesystem::directory_iterator("labs")) {
        if (entry.path().extension() != ".json") continue;
        try {
            Lab::loadFile(entry.path());
        } catch (const std::exception& e) {
            std::cerr << "  " << pathToUtf8(entry.path()) << ": " << e.what() << "\n";
            ++failures;
        }
    }
}

TEST(grader_evaluates_expectations) {
    LabCheck c;
    c.description = "perm";
    c.expectExitCode = 0;
    c.expectOutput = "770";
    CHECK(evaluateCheck(c, {0, "770\n", "", false}).passed);
    CHECK(!evaluateCheck(c, {0, "755\n", "", false}).passed);
    CHECK(!evaluateCheck(c, {1, "770", "", false}).passed);
    CHECK(!evaluateCheck(c, {0, "", "", true}).passed);

    LabCheck r;
    r.expectRegex = "^inet 10\\.0\\.0\\.[0-9]+";
    CHECK(evaluateCheck(r, {0, "inet 10.0.0.5/24", "", false}).passed);
    CHECK(!evaluateCheck(r, {0, "inet 192.168.1.5/24", "", false}).passed);
}

TEST(tutor_respects_lab_policy) {
    Settings s;
    s.tutorMode = "explain";
    LabAiPolicy locked;
    locked.mode = "coach";
    CHECK(AiTutor::effectiveMode(s, &locked) == TutorMode::Coach);
    CHECK(AiTutor::effectiveMode(s, nullptr) == TutorMode::Explain);

    LabAiPolicy exam;
    exam.examMode = true;
    CHECK_THROWS(AiTutor::effectiveMode(s, &exam));
    s.aiEnabled = false;
    CHECK_THROWS(AiTutor::effectiveMode(s, nullptr));
}

TEST(tutor_request_shape) {
    Settings s;
    AiTutor tutor(s);
    Lab lab = Lab::parse(kLab);
    TutorContext ctx;
    ctx.lab = &lab;
    ctx.task = &lab.tasks[0];
    auto req = tutor.buildRequest(TutorMode::Coach, ctx, nlohmann::json::array({{{"role", "user"}, {"content", "hi"}}}));
    CHECK(req["model"] == "claude-opus-5-5");
    CHECK(req["output_config"]["effort"] == "low");
    CHECK(req["fallbacks"] == "default");
    CHECK(!req.contains("thinking"));
    CHECK(req["system"].size() == 2);
    CHECK(req["system"][1].contains("cache_control"));
    CHECK(req["system"][0]["text"].get<std::string>().find("COACH") != std::string::npos);

    std::string turn = AiTutor::userTurn("why?", ctx);
    CHECK(turn.find("<current_task id=\"a\">") != std::string::npos);
    CHECK(endsWith(turn, "why?"));
}

TEST(tutor_parses_responses) {
    auto ok = AiTutor::parseResponse(200, R"({"content":[{"type":"thinking","thinking":""},{"type":"text","text":"Try ls -l"}],
                                             "stop_reason":"end_turn","usage":{"input_tokens":10,"output_tokens":5}})");
    CHECK(ok.text == "Try ls -l");
    CHECK(!ok.refused);
    CHECK(ok.outputTokens == 5);

    auto refused = AiTutor::parseResponse(200, R"({"content":[],"stop_reason":"refusal","stop_details":null})");
    CHECK(refused.refused);

    CHECK(AiTutor::parseResponse(200, R"({"content":[{"type":"text","text":"x"}],"stop_reason":"max_tokens"})").truncated);
    CHECK_THROWS(AiTutor::parseResponse(401, R"({"type":"error","error":{"type":"authentication_error","message":"bad"}})"));
}

TEST(update_and_catalog_parsing) {
    auto info = parseReleaseJson(R"({"tag_name":"v0.2.0","html_url":"https://x","body":"notes"})", "0.1.0");
    CHECK(info.available);
    CHECK(info.latestVersion == "0.2.0");
    CHECK(!parseReleaseJson(R"({"tag_name":"v0.1.0"})", "0.1.0").available);

    auto cat = parseLabCatalog(R"({"labs":[{"id":"a","url":"https://x/a.json","version":"1.1.0"}]})");
    CHECK(cat.size() == 1 && cat[0].version == "1.1.0");
    CHECK_THROWS(parseLabCatalog("{}"));
}

int main() {
    for (auto& [name, fn] : registry()) {
        int before = failures;
        try {
            fn();
        } catch (const std::exception& e) {
            std::cerr << "  unexpected exception: " << e.what() << "\n";
            ++failures;
        }
        std::cout << (failures == before ? "[pass] " : "[FAIL] ") << name << "\n";
    }
    std::cout << (failures ? "\nFAILED: " : "\nOK: ") << registry().size() << " tests, " << failures << " failed check(s)\n";
    return failures ? 1 : 0;
}
