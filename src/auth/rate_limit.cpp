#include "auth/rate_limit.h"

namespace auth {
namespace {

constexpr std::size_t kPruneThreshold = 10'000;

} // namespace

bool RateLimiter::allow(const std::string& key, Clock::time_point now)
{
    std::lock_guard lock(mutex_);
    if (events_.size() > kPruneThreshold) prune(now);

    auto& q = events_[key];
    while (!q.empty() && now - q.front() >= window_) q.pop_front();
    if (q.size() >= max_events_) return false;
    q.push_back(now);
    return true;
}

void RateLimiter::prune(Clock::time_point now)
{
    for (auto it = events_.begin(); it != events_.end();) {
        if (it->second.empty() || now - it->second.back() >= window_)
            it = events_.erase(it);
        else
            ++it;
    }
}

} // namespace auth
