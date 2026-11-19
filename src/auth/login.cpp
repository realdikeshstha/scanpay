#include "auth/login.h"

#include "auth/password.h"
#include "security_log.h"
#include "validate.h"

namespace auth {

LoginResult LoginService::login(std::string_view phone_input, std::string_view password, const std::string& ip)
{
    if (!ip_limiter_.allow("login:" + ip)) {
        seclog::event("login_rate_limited", {{"ip", ip}});
        return {LoginStatus::RateLimited, std::nullopt};
    }

    const auto phone = validate::phone(phone_input);
    if (!phone || password.empty() || password.size() > validate::kMaxPasswordBytes) {
        seclog::event("login_failure", {{"reason", "invalid_input"}, {"ip", ip}});
        return {};
    }

    std::int64_t user_id = 0;
    std::string hash;
    {
        auto conn = pool_.acquire();
        pqxx::read_transaction tx(*conn);
        auto rows = db::exec(tx,
                             "SELECT id, password_hash, COALESCE(locked_until > now(), false) "
                             "FROM users WHERE phone = $1",
                             *phone);
        if (rows.empty()) {
            burn_verification(password);
            seclog::event("login_failure",
                          {{"reason", "unknown_account"}, {"phone", validate::mask_phone(*phone)}, {"ip", ip}});
            return {};
        }
        user_id = rows[0][0].as<std::int64_t>();
        hash = rows[0][1].as<std::string>();
        if (rows[0][2].as<bool>()) {
            seclog::event("login_blocked_locked", {{"user_id", std::to_string(user_id)}, {"ip", ip}});
            return {};
        }
    }

    // Argon2id verification is deliberately slow; no DB connection is held.
    const bool ok = verify_password(hash, password);

    auto conn = pool_.acquire();
    pqxx::work tx(*conn);
    if (!ok) {
        // Atomic increment; the 5th failure locks the account and resets the counter.
        auto row = db::exec(tx,
                            "UPDATE users SET "
                            "  locked_until = CASE WHEN failed_logins + 1 >= $2 "
                            "                 THEN now() + make_interval(secs => $3) ELSE locked_until END, "
                            "  failed_logins = CASE WHEN failed_logins + 1 >= $2 THEN 0 ELSE failed_logins + 1 END "
                            "WHERE id = $1 "
                            "RETURNING COALESCE(locked_until > now(), false), failed_logins",
                            user_id, kMaxFailedLogins, static_cast<std::int64_t>(kLockoutDuration.count()))
                       .one_row();
        tx.commit();
        const bool now_locked = row[0].as<bool>();
        seclog::event("login_failure",
                      {{"reason", "bad_password"},
                       {"user_id", std::to_string(user_id)},
                       {"failed_count", now_locked ? std::to_string(kMaxFailedLogins) : row[1].as<std::string>()},
                       {"ip", ip}});
        if (now_locked)
            seclog::event("account_locked", {{"user_id", std::to_string(user_id)}, {"minutes", "15"}, {"ip", ip}});
        return {};
    }

    // Re-check the lock in the same statement in case a concurrent attempt
    // locked the account while this password was being verified.
    auto cleared = db::exec(tx,
                            "UPDATE users SET failed_logins = 0, locked_until = NULL "
                            "WHERE id = $1 AND NOT COALESCE(locked_until > now(), false) RETURNING id",
                            user_id);
    tx.commit();
    if (cleared.empty()) {
        seclog::event("login_blocked_locked", {{"user_id", std::to_string(user_id)}, {"ip", ip}});
        return {};
    }

    LoginResult result{LoginStatus::Success, sessions_.create(user_id)};
    seclog::event("login_success", {{"user_id", std::to_string(user_id)}, {"ip", ip}});
    return result;
}

} // namespace auth
