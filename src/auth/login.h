#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

#include "auth/rate_limit.h"
#include "auth/session.h"
#include "db/pool.h"

namespace auth {

inline constexpr int kMaxFailedLogins = 5;
inline constexpr std::chrono::seconds kLockoutDuration = std::chrono::minutes(15);

enum class LoginStatus {
    Success,
    Failed,       // bad phone/password, unknown account, or locked account
    RateLimited,  // too many attempts from this IP
};

struct LoginResult {
    LoginStatus status = LoginStatus::Failed;
    std::optional<NewSession> session;
};

// Phone + password login with Argon2id verification, per-account lockout
// (5 failures -> 15 minutes) and per-IP rate limiting. All failure reasons
// look identical to the caller; the reason is only in the security log.
class LoginService {
public:
    LoginService(db::Pool& pool, SessionStore& sessions, RateLimiter& ip_limiter)
        : pool_(pool), sessions_(sessions), ip_limiter_(ip_limiter) {}

    LoginResult login(std::string_view phone_input, std::string_view password, const std::string& ip);

private:
    db::Pool& pool_;
    SessionStore& sessions_;
    RateLimiter& ip_limiter_;
};

} // namespace auth
