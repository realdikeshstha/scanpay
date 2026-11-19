#pragma once

#include <chrono>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>

namespace auth {

// Sliding-window limiter: at most `max_events` per key within `window`.
// In-memory, so limits reset when the server restarts.
class RateLimiter {
public:
    using Clock = std::chrono::steady_clock;

    RateLimiter(std::size_t max_events, std::chrono::seconds window)
        : max_events_(max_events), window_(window) {}

    // Records an attempt and returns whether it is allowed.
    bool allow(const std::string& key) { return allow(key, Clock::now()); }
    bool allow(const std::string& key, Clock::time_point now);

private:
    void prune(Clock::time_point now);

    std::size_t max_events_;
    std::chrono::seconds window_;
    std::mutex mutex_;
    std::unordered_map<std::string, std::deque<Clock::time_point>> events_;
};

} // namespace auth
