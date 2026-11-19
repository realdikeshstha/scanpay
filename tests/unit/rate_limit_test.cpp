#include <doctest/doctest.h>

#include "auth/rate_limit.h"

TEST_CASE("rate limiter allows N events per window per key")
{
    using namespace std::chrono_literals;
    auth::RateLimiter limiter(3, 60s);
    const auto t0 = auth::RateLimiter::Clock::now();

    CHECK(limiter.allow("ip:1", t0));
    CHECK(limiter.allow("ip:1", t0 + 1s));
    CHECK(limiter.allow("ip:1", t0 + 2s));
    CHECK_FALSE(limiter.allow("ip:1", t0 + 3s));
    CHECK(limiter.allow("ip:2", t0 + 3s));  // keys are independent

    const auto later = t0 + 60s + 500ms;  // only the first event has left the window
    CHECK(limiter.allow("ip:1", later));
    CHECK_FALSE(limiter.allow("ip:1", later));
}
