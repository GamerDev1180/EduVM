# EduVM

A simple, free desktop app for creating and running virtual machines, in the style of VMware Workstation.

* **New Virtual Machine wizard**: pick an installer ISO, and EduVM recognizes the operating system (Ubuntu, Debian, Windows 10/11, Kali, ...) and picks sensible memory and disk sizes.
* **Console inside the app**: the VM's screen is shown in the EduVM window. Keyboard input is sent as scancodes, so the guest's own keyboard layout applies. Click to control the VM, press **Ctrl+Alt** to release, and press **Ctrl+Alt+Enter** for full screen.
* **Virtual Machine Settings**: memory, processors, hard disk (expand), CD/DVD (ISO), network adapter (NAT, SSH forwarding, LAN segments), display and options.
* **Snapshots**: take, restore and delete snapshots.
* **Clone** a VM, or **import** an existing disk from VMware (`.vmdk`), VirtualBox (`.vdi`) or Hyper-V (`.vhd`/`.vhdx`).
* **Hardware acceleration** with Windows Hypervisor Platform (WHPX), KVM (Linux) or Hypervisor.framework (macOS).
* VMs **keep running when you close the app** if you choose; EduVM reconnects when it starts again.

## Getting started

1. Install QEMU, which runs the VMs: `winget install SoftwareFreedomConservancy.QEMU` (Linux: `apt install qemu-system-x86 qemu-utils`, macOS: `brew install qemu`).
2. On Windows, enable **Windows Hypervisor Platform** under *Turn Windows features on or off* and reboot.
3. Start EduVM and click **Create a New Virtual Machine**. If something is missing, the system check opens and explains how to fix it.

## Building

You need CMake 3.20+ and a C++20 compiler (MSVC 2022+, GCC 11+ or Clang 14+). nlohmann/json, GLFW and Dear ImGui are downloaded automatically. On Linux, you also need the libcurl, X11/Wayland and OpenGL development packages.

```
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The app is `build/Release/EduVM.exe` (or `build/EduVM`).

## How it works

Each VM runs as its own QEMU process. EduVM controls QEMU through QMP, its JSON control channel, and shows the VM's screen through a local VNC connection (127.0.0.1 only) that it renders inside the window.

```
src/app/     desktop UI (Dear ImGui + GLFW + OpenGL)
  App.cpp          window layout: menu, library, home page, full screen, status bar
  AppVm.cpp        VM page: toolbar, Console / Summary / Snapshots tabs
  AppDialogs.cpp   New VM wizard, VM settings, import, clone, preferences, system check
  VmScreen         renders the VM display, forwards mouse and keyboard
  StatusMonitor    polls VM state in the background
  Jobs.hpp         runs slow work off the UI thread
src/core/    engine, independent of the UI
  QemuBackend      QEMU command lines, power, snapshots, disks (qemu-img)
  RfbClient        VNC client for the console
  QmpClient        QEMU control channel
  VmRepository     one folder per VM: vm.json, disk.qcow2, runtime.json, qemu.log
  OsDetect         guest OS detection from ISO file names
  HostInfo / Diagnostics   hardware and acceleration detection, system check
  Updater          checks for new EduVM releases
tests/       unit tests for core
```

VMs are stored in `%APPDATA%\EduVM\vms` (Windows) or `~/.local/share/eduvm/vms`. Set `EDUVM_HOME` to use another location.

### Known limitation: memory snapshots on Windows

Windows Hypervisor Platform cannot save a VM's memory state. On Windows, snapshots of a running VM therefore save the disk only, and restoring one powers the VM off first; the app handles this for you. With KVM or HVF, snapshots of running VMs include memory.

## Roadmap

- [ ] Clipboard sharing and shared folders
- [ ] Change the CD/DVD while the VM is running
- [ ] USB device passthrough
- [ ] Faster VNC encodings for high-resolution desktops
- [ ] Installer and auto-update (MSI / winget, .deb, Homebrew)
