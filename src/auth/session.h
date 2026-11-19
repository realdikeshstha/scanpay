#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "db/pool.h"

namespace auth {

inline constexpr std::chrono::seconds kIdleTimeout = std::chrono::minutes(30);
inline constexpr std::chrono::seconds kAbsoluteLifetime = std::chrono::hours(12);

struct Session {
    std::int64_t user_id = 0;
    std::string token_hash;  // SHA-256 of the cookie value; what the DB stores
    std::string csrf_token;
    std::string full_name;
    std::string phone;
    std::string role;

    bool is_merchant() const { return role == "merchant"; }
};

struct NewSession {
    std::string token;  // goes into the cookie, never stored server-side
    Session session;
};

// Server-side sessions stored in PostgreSQL. The browser holds a random
// 256-bit token; the database holds only its hash.
class SessionStore {
public:
    explicit SessionStore(db::Pool& pool) : pool_(pool) {}

    NewSession create(std::int64_t user_id);

    // Returns the session and refreshes its idle timer, or nullopt if the token
    // is unknown, idle for more than 30 minutes, or past its absolute lifetime.
    // Expired sessions are deleted.
    std::optional<Session> lookup(std::string_view token);

    void destroy(std::string_view token);

private:
    db::Pool& pool_;
};

} // namespace auth
