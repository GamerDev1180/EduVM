# EduVM

Virtual machine labs for ICT students. EduVM runs VMs the way VMware Workstation does, plus what classrooms need: guided labs, automatic grading inside the VM, one-click resets, and an AI tutor that coaches students instead of handing them the answers.

## Why not just VMware or VirtualBox?

| | VMware / VirtualBox | EduVM |
|---|---|---|
| Run VMs with hardware acceleration | Yes | Yes (QEMU + WHPX / KVM / HVF) |
| Snapshots | Yes | Yes: live (RAM included) and disk-only |
| Guided lab assignments | No | Lab manifests with tasks, hints and points |
| Checks whether the student did it right | No | Auto-grader runs checks *inside* the VM via the guest agent |
| "I broke it" recovery | Manual snapshot juggling | `eduvm lab reset`: copy-on-write overlays restore in seconds |
| Multi-VM networks without admin rights | Host-only adapters, drivers | Lab segments (virtual switches over multicast sockets) |
| Help when stuck | Google | AI tutor that knows the lab, the task and the grader feedback |
| Hand-in | Screenshots | Generated report with score, hints and AI usage |
| New course content | n/a | Lab catalog: instructors publish, students run `eduvm lab update` |
| Cost | Licences | Free, open source |

## Quick start

1. Install QEMU: `winget install SoftwareFreedomConservancy.QEMU` (Linux: `apt install qemu-system-x86 qemu-utils`, macOS: `brew install qemu`).
2. On Windows, enable **Windows Hypervisor Platform** under *Turn Windows features on or off* and reboot.
3. Build EduVM (below), then run:

```
eduvm doctor                          # checks QEMU, acceleration, RAM, disk, AI key
eduvm lab install labs/linux-basics-01.json
eduvm lab start linux-basics-01
eduvm lab check linux-basics-01       # grade your work
eduvm lab hint linux-basics-01 shared-folder
eduvm ai ask "why does chmod say operation not permitted?" --lab linux-basics-01
eduvm report linux-basics-01 --out report.md
```

Plain VMs work too:

```
eduvm vm create ubuntu --iso ubuntu-24.04.iso --memory 4096
eduvm vm start ubuntu
eduvm snap create ubuntu fresh-install
```

Run `eduvm help` for every command.

## Building

You need CMake 3.20+ and a C++20 compiler (MSVC 2022+, GCC 11+ or Clang 14+). nlohmann/json is downloaded automatically. On Linux and macOS, libcurl is also required (`apt install libcurl4-openssl-dev`).

```
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Architecture

```
src/cli/main.cpp         thin command-line front end
src/core/                everything else, reusable by a future GUI
  QemuBackend            builds QEMU command lines, starts/stops VMs, snapshots
  QmpClient              QEMU Machine Protocol (JSON control channel of a running VM)
  GuestAgent             qemu-guest-agent client: runs commands inside the VM
  VmRepository           VM folders: vm.json, disk.qcow2, runtime.json, qemu.log
  HostInfo               CPU/RAM/disk + acceleration detection, safe resource caps
  Lab / LabRunner        lab manifests -> VMs (copy-on-write overlays), reset
  Grader                 executes lab checks in the guest and compares output
  Progress               per-lab progress and the hand-in report
  AiTutor                Claude-powered tutor with instructor-controlled policy
  Updater                app release check and lab catalog
  Http / Process / TcpSocket   small platform layers (WinHTTP or libcurl, Win32 or POSIX)
```

EduVM uses QEMU as its execution engine rather than its own hypervisor. Writing a hypervisor is a multi-year effort, and QEMU on top of WHPX, KVM or HVF already gives near-native speed. The value EduVM adds is the classroom layer.

All state lives in `%APPDATA%\EduVM` (Windows) or `~/.local/share/eduvm`. Set `EDUVM_HOME` to override this, which is useful for USB sticks or shared lab PCs.

## Writing labs (for instructors)

A lab is one JSON file. See [labs/linux-basics-01.json](labs/linux-basics-01.json) and the multi-VM [labs/networking-01.json](labs/networking-01.json).

```jsonc
{
  "id": "linux-basics-01", "title": "...", "version": "1.0.0", "level": "beginner",
  "ai": { "mode": "coach", "maxQuestionsPerTask": 5, "examMode": false },
  "vms": [{ "name": "server", "guestOs": "linux", "memoryMb": 1024,
            "baseImage": "debian-12-eduvm.qcow2", "imageUrl": "https://...",
            "segments": ["lan"] }],
  "tasks": [{
    "id": "shared-folder", "title": "...", "instructions": "...", "points": 30,
    "hints": ["revealed one at a time"],
    "checks": [{ "vm": "server", "description": "permissions are 770",
                 "command": "stat -c %a /srv/sales", "expectOutput": "770" }]
  }]
}
```

* **Checks** run with `/bin/sh -c` (Linux guests) or PowerShell (Windows guests). The available expectations are `expectExitCode` (the default is `0`), `expectOutput` (an exact match after trimming), `expectContains` and `expectRegex`.
* **Base images** must have `qemu-guest-agent` installed and enabled, because grading depends on it. Students get a copy-on-write overlay, so the base image is never modified.
* **Segments**: VMs that list the same segment name share a virtual switch, and segments are scoped to their lab.
* **AI policy**: `mode` locks the tutor to `coach` (hints only) or `explain`. `examMode` disables both the tutor and the hints.
* **Publishing updates**: host a catalog file such as `{"labs":[{"id":"...","version":"1.1.0","url":"https://.../lab.json"}]}` and have students run `eduvm config set labCatalogUrl <url>`. After that, `eduvm lab update` installs any lab whose version is newer than the one they have.

## AI tutor

The tutor calls the Claude API (`claude-opus-5-5` by default, with low effort for fast conversational answers). The request includes the lab, the current task, the hints already revealed, the grader's latest feedback, and any output the student attaches with `--file`. Conversations are kept per task; `eduvm ai reset` starts a fresh one.

* **Coach mode** (the default): explains concepts, gives the next small step, and gets more concrete as the student shows effort. It does not give copy-paste solutions.
* **Explain mode**: gives full explanations with commands, always including *why* they work.
* It replies in the language the student writes in, for example Dutch.
* Every question is counted in the progress report, so the use of AI is visible to teachers rather than hidden.

**Keys:** students can set `ANTHROPIC_API_KEY`. The better setup for schools is a small proxy that holds the key: `eduvm config set aiBaseUrl https://ai.school.example` and `eduvm config set apiKeyEnv ""`. This keeps keys off student laptops and gives the school one place for budgets and logging.

## Roadmap

- [ ] Desktop GUI (Qt 6) on top of `eduvm_core`: VM library, embedded display, lab sidebar with tasks, check button and tutor chat
- [ ] Teacher dashboard: collect reports from a class, see where students get stuck
- [ ] Image builder: produce lab base images (with the guest agent preinstalled) from cloud images
- [ ] Self-updating installer (MSI / winget, .deb, Homebrew)
- [ ] Shared folders (virtiofs) and clipboard integration
- [ ] Offline tutor fallback for classrooms without internet
- [ ] Tutor can run read-only diagnostics in the VM (e.g. `ip addr`) through tool use
