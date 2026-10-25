// Dependency-free unit tests for the parts of EduVM that don't need QEMU or
// network access. Run: ctest --test-dir build -C Release

#include <algorithm>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include "core/OsDetect.hpp"
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
    c.displayName = "Web Server 1";
    c.segments = {"lab/lan"};
    c.display = DisplayMode::Headless;
    c.nat = false;
    VmConfig back = nlohmann::json(c).get<VmConfig>();
    CHECK(back.name == "web1");
    CHECK(vmTitle(back) == "Web Server 1");
    CHECK(!back.nat);
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
    spec.runtime.qmpPort = 4444;
    spec.runtime.sshPort = 2222;
    spec.runtime.vncPort = 5907;
    spec.accelerator = Accelerator::Whpx;
    auto args = buildQemuArgs(spec);

    CHECK(contains(args, "whpx,kernel-irqchip=off"));
    CHECK(contains(args, "tcg"));
    CHECK(contains(args, "user,id=net0,hostfwd=tcp:127.0.0.1:2222-:22"));
    CHECK(contains(args, "tcp:127.0.0.1:4444,server=on,wait=off"));
    CHECK(contains(args, "file=disk.qcow2,format=qcow2,if=virtio"));
    CHECK(contains(args, "127.0.0.1:7"));  // embedded display: VNC display 7 = port 5907
    CHECK(contains(args, "none"));
    bool hasSegment = std::any_of(args.begin(), args.end(), [](const std::string& s) { return startsWith(s, "socket,id=seg0,mcast=230.0.0.1:"); });
    CHECK(hasSegment);
}

TEST(windows_guests_get_compatible_devices) {
    QemuLaunchSpec spec;
    spec.qemuBinary = "qemu-system-x86_64";
    spec.diskPath = "disk.qcow2";
    spec.config.name = "win";
    spec.config.guestOs = GuestOs::Windows;
    spec.config.display = DisplayMode::Window;
    auto args = buildQemuArgs(spec);
    CHECK(!contains(args, "-vnc"));
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

TEST(nat_off_without_segments_disables_networking) {
    QemuLaunchSpec spec;
    spec.qemuBinary = "qemu-system-x86_64";
    spec.diskPath = "disk.qcow2";
    spec.config.name = "isolated";
    spec.config.nat = false;
    auto args = buildQemuArgs(spec);
    CHECK(!std::any_of(args.begin(), args.end(), [](const std::string& s) { return startsWith(s, "user,"); }));
    CHECK(contains(args, "-nic"));
}

TEST(slugify_makes_folder_safe_ids) {
    CHECK(slugify("Ubuntu Server 24.04") == "Ubuntu-Server-24-04");
    CHECK(slugify("  ") == "vm");
    CHECK(slugify("Windows 11 (test)") == "Windows-11-test");
    CHECK(isValidVmName(slugify("Ünïcødé name!")));
}

TEST(os_is_detected_from_iso_names) {
    auto ubuntu = guessOsFromIso("C:/isos/ubuntu-24.04.1-desktop-amd64.iso");
    CHECK(ubuntu.detected && ubuntu.os == GuestOs::Linux);
    CHECK(ubuntu.name == "Ubuntu 24.04.1");
    CHECK(ubuntu.memoryMb == 4096);

    auto win = guessOsFromIso("Win11_24H2_English_x64.iso");
    CHECK(win.detected && win.os == GuestOs::Windows && win.name == "Windows 11");
    CHECK(guessOsFromIso("en-us_windows_server_2022.iso").name == "Windows Server 2022");

    auto alpine = guessOsFromIso("alpine-virt-3.20.3-x86_64.iso");
    CHECK(alpine.os == GuestOs::Linux && alpine.memoryMb == 1024);

    CHECK(!guessOsFromIso("mystery.iso").detected);
}

TEST(update_parsing) {
    auto info = parseReleaseJson(R"({"tag_name":"v0.2.0","html_url":"https://x","body":"notes"})", "0.1.0");
    CHECK(info.available);
    CHECK(info.latestVersion == "0.2.0");
    CHECK(!parseReleaseJson(R"({"tag_name":"v0.1.0"})", "0.1.0").available);
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
