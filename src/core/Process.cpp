#include "core/Process.hpp"

#include "core/Strings.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#endif

namespace fs = std::filesystem;

namespace eduvm {

#ifdef _WIN32

// Quoting rules from "Parsing C++ command-line arguments" (MSVC CRT).
static void appendQuoted(std::string& cmd, const std::string& arg) {
    if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string::npos) {
        cmd += arg;
        return;
    }
    cmd += '"';
    for (size_t i = 0;; ++i) {
        size_t backslashes = 0;
        while (i < arg.size() && arg[i] == '\\') {
            ++i;
            ++backslashes;
        }
        if (i == arg.size()) {
            cmd.append(backslashes * 2, '\\');
            break;
        }
        if (arg[i] == '"') {
            cmd.append(backslashes * 2 + 1, '\\');
            cmd += '"';
        } else {
            cmd.append(backslashes, '\\');
            cmd += arg[i];
        }
    }
    cmd += '"';
}

std::string buildWindowsCommandLine(const std::vector<std::string>& argv) {
    std::string cmd;
    for (size_t i = 0; i < argv.size(); ++i) {
        if (i) cmd += ' ';
        appendQuoted(cmd, argv[i]);
    }
    return cmd;
}

static std::string lastErrorMessage() {
    DWORD code = GetLastError();
    wchar_t* buf = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<wchar_t*>(&buf), 0, nullptr);
    std::string msg = buf ? trim(narrow(buf)) : "error " + std::to_string(code);
    if (buf) LocalFree(buf);
    return msg;
}

ProcessResult runAndCapture(const std::vector<std::string>& argv) {
    if (argv.empty()) throw Error("runAndCapture: empty argv");

    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE readPipe = nullptr, writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) throw Error("CreatePipe failed: " + lastErrorMessage());
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    std::wstring cmd = widen(buildWindowsCommandLine(argv));
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                             &pi);
    CloseHandle(writePipe);
    if (!ok) {
        CloseHandle(readPipe);
        throw Error("Cannot start " + argv[0] + ": " + lastErrorMessage());
    }

    ProcessResult result;
    char buf[4096];
    DWORD n = 0;
    while (ReadFile(readPipe, buf, sizeof(buf), &n, nullptr) && n > 0) result.output.append(buf, n);
    CloseHandle(readPipe);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    result.exitCode = static_cast<int>(code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return result;
}

long spawnDetached(const std::vector<std::string>& argv, const fs::path& logFile) {
    if (argv.empty()) throw Error("spawnDetached: empty argv");

    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE log = CreateFileW(logFile.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) throw Error("Cannot open log " + pathToUtf8(logFile) + ": " + lastErrorMessage());

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = log;
    si.hStdError = log;
    si.hStdInput = nullptr;

    std::wstring cmd = widen(buildWindowsCommandLine(argv));
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE,
                             DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &si, &pi);
    CloseHandle(log);
    if (!ok) throw Error("Cannot start " + argv[0] + ": " + lastErrorMessage());

    long pid = static_cast<long>(pi.dwProcessId);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return pid;
}

bool isProcessAlive(long pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!h) return false;
    DWORD code = 0;
    bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return alive;
}

bool killProcess(long pid) {
    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
    if (!h) return false;
    bool ok = TerminateProcess(h, 1) != 0;
    CloseHandle(h);
    return ok;
}

#else  // POSIX

ProcessResult runAndCapture(const std::vector<std::string>& argv) {
    if (argv.empty()) throw Error("runAndCapture: empty argv");
    int fds[2];
    if (pipe(fds) != 0) throw Error(std::string("pipe failed: ") + std::strerror(errno));

    pid_t pid = fork();
    if (pid < 0) throw Error(std::string("fork failed: ") + std::strerror(errno));
    if (pid == 0) {
        dup2(fds[1], STDOUT_FILENO);
        dup2(fds[1], STDERR_FILENO);
        close(fds[0]);
        close(fds[1]);
        std::vector<char*> args;
        for (auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
        args.push_back(nullptr);
        execvp(args[0], args.data());
        _exit(127);
    }
    close(fds[1]);
    ProcessResult result;
    char buf[4096];
    ssize_t n;
    while ((n = read(fds[0], buf, sizeof(buf))) > 0) result.output.append(buf, static_cast<size_t>(n));
    close(fds[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return result;
}

long spawnDetached(const std::vector<std::string>& argv, const fs::path& logFile) {
    if (argv.empty()) throw Error("spawnDetached: empty argv");
    pid_t pid = fork();
    if (pid < 0) throw Error(std::string("fork failed: ") + std::strerror(errno));
    if (pid == 0) {
        setsid();
        int log = open(logFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (log >= 0) {
            dup2(log, STDOUT_FILENO);
            dup2(log, STDERR_FILENO);
            close(log);
        }
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) dup2(devnull, STDIN_FILENO);
        std::vector<char*> args;
        for (auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
        args.push_back(nullptr);
        execvp(args[0], args.data());
        _exit(127);
    }
    return static_cast<long>(pid);
}

bool isProcessAlive(long pid) {
    // Reap it if it is our exited child, otherwise kill(0) probes existence.
    int status = 0;
    if (waitpid(static_cast<pid_t>(pid), &status, WNOHANG) == pid) return false;
    return kill(static_cast<pid_t>(pid), 0) == 0;
}

bool killProcess(long pid) { return kill(static_cast<pid_t>(pid), SIGKILL) == 0; }

#endif

std::optional<fs::path> findExecutable(const std::string& name, const std::vector<fs::path>& extraDirs) {
#ifdef _WIN32
    const std::string exe = endsWith(toLower(name), ".exe") ? name : name + ".exe";
    const char listSep = ';';
#else
    const std::string& exe = name;
    const char listSep = ':';
#endif
    std::vector<fs::path> dirs = extraDirs;
    if (auto path = getEnv("PATH")) {
        for (auto& d : split(*path, listSep))
            if (!d.empty()) dirs.push_back(utf8ToPath(d));
    }
    for (auto& d : dirs) {
        std::error_code ec;
        fs::path candidate = d / utf8ToPath(exe);
        if (fs::is_regular_file(candidate, ec)) return candidate;
    }
    return std::nullopt;
}

}  // namespace eduvm
