#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "core/QemuBackend.hpp"
#include "core/VmRepository.hpp"

namespace eduvm::app {

struct VmStatus {
    VmConfig config;
    VmState state = VmState::Stopped;
    std::optional<VmRuntime> runtime;
};

// Polls VM state in the background (querying QEMU every frame would stall
// the UI) and publishes a snapshot the UI can read cheaply.
class StatusMonitor {
public:
    using BackendGetter = std::function<std::shared_ptr<const QemuBackend>()>;

    StatusMonitor(const VmRepository& repo, BackendGetter backend);
    ~StatusMonitor();

    void start();
    void stop();
    void refreshSoon();  // e.g. right after starting or stopping a VM

    std::vector<VmStatus> snapshot() const;
    std::optional<VmStatus> find(const std::string& name) const;

private:
    void loop();
    void refresh();

    const VmRepository& repo_;
    BackendGetter backend_;
    mutable std::mutex mutex_;
    std::vector<VmStatus> statuses_;
    std::condition_variable wake_;
    bool poked_ = false;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

}  // namespace eduvm::app
