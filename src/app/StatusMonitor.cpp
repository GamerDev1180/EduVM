#include "app/StatusMonitor.hpp"

#include <map>

namespace eduvm::app {

StatusMonitor::StatusMonitor(const VmRepository& repo, BackendGetter backend) : repo_(repo), backend_(std::move(backend)) {}

StatusMonitor::~StatusMonitor() { stop(); }

void StatusMonitor::start() {
    if (running_) return;
    running_ = true;
    refresh();  // have data for the very first frame
    thread_ = std::thread([this] { loop(); });
}

void StatusMonitor::stop() {
    if (!running_) return;
    running_ = false;
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void StatusMonitor::refreshSoon() {
    {
        std::lock_guard lock(mutex_);
        poked_ = true;
    }
    wake_.notify_all();
}

std::vector<VmStatus> StatusMonitor::snapshot() const {
    std::lock_guard lock(mutex_);
    return statuses_;
}

std::optional<VmStatus> StatusMonitor::find(const std::string& name) const {
    std::lock_guard lock(mutex_);
    for (auto& s : statuses_)
        if (s.config.name == name) return s;
    return std::nullopt;
}

void StatusMonitor::loop() {
    while (running_) {
        {
            std::unique_lock lock(mutex_);
            wake_.wait_for(lock, std::chrono::milliseconds(1500), [this] { return poked_ || !running_; });
            poked_ = false;
        }
        if (!running_) break;
        refresh();
    }
}

void StatusMonitor::refresh() {
    auto backend = backend_();
    std::map<std::string, VmState> previous;
    {
        std::lock_guard lock(mutex_);
        for (auto& s : statuses_) previous[s.config.name] = s.state;
    }

    std::vector<VmStatus> fresh;
    for (auto& cfg : repo_.list()) {
        VmStatus s;
        s.config = cfg;
        try {
            s.runtime = repo_.loadRuntime(cfg.name);
            s.state = backend->state(cfg.name);
            // QMP serves one client at a time; if it was busy, keep the last known state.
            if (s.state == VmState::Unknown && s.runtime && previous.count(cfg.name)) s.state = previous[cfg.name];
        } catch (const std::exception&) {
            s.state = VmState::Unknown;
        }
        fresh.push_back(std::move(s));
    }
    std::lock_guard lock(mutex_);
    statuses_ = std::move(fresh);
}

}  // namespace eduvm::app
