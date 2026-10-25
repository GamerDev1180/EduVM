#pragma once

#include <algorithm>
#include <exception>
#include <functional>
#include <future>
#include <string>
#include <vector>

namespace eduvm::app {

// Code to run back on the UI thread once background work is done.
using UiCallback = std::function<void()>;

// Runs slow work (starting VMs, grading, AI calls, downloads) off the UI
// thread. The work returns a UiCallback, which poll() runs on the UI thread,
// so all UI state is only ever touched from one thread.
class JobRunner {
public:
    void run(std::string label, std::function<UiCallback()> work) {
        jobs_.push_back({std::move(label), std::async(std::launch::async, std::move(work))});
    }

    // Call once per frame. Returns "label: message" for every failed job.
    std::vector<std::string> poll() {
        std::vector<Job> finished;
        for (auto it = jobs_.begin(); it != jobs_.end();) {
            if (it->future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                finished.push_back(std::move(*it));
                it = jobs_.erase(it);
            } else {
                ++it;
            }
        }
        std::vector<std::string> errors;
        for (auto& job : finished) {  // callbacks may start new jobs, so run them after erasing
            try {
                if (UiCallback cb = job.future.get()) cb();
            } catch (const std::exception& e) {
                errors.push_back(e.what());
            }
        }
        return errors;
    }

    bool busy(const std::string& label) const {
        return std::any_of(jobs_.begin(), jobs_.end(), [&](const Job& j) { return j.label == label; });
    }
    bool busyPrefix(const std::string& prefix) const {
        return std::any_of(jobs_.begin(), jobs_.end(), [&](const Job& j) { return j.label.rfind(prefix, 0) == 0; });
    }
    bool any() const { return !jobs_.empty(); }
    std::vector<std::string> labels() const {
        std::vector<std::string> out;
        for (auto& j : jobs_) out.push_back(j.label);
        return out;
    }

    // Blocks until all jobs finished (used on exit so threads don't outlive the app).
    void waitAll() {
        for (auto& j : jobs_) j.future.wait();
    }

private:
    struct Job {
        std::string label;
        std::future<UiCallback> future;
    };
    std::vector<Job> jobs_;
};

}  // namespace eduvm::app
