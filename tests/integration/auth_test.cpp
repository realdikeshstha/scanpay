// Authentication, session and injection tests against the real database.

#include <doctest/doctest.h>

#include "crypto.h"
#include "fixture.h"

using auth::LoginStatus;

TEST_CASE("correct credentials create a session; wrong ones do not")
{
    test::Env env;
    const auto ok = env.svc->login.login(test::kAlicePhone, test::kAlicePassword, "10.0.0.1");
    REQUIRE(ok.status == LoginStatus::Success);
    CHECK(ok.session->session.user_id == env.alice);

    const auto bad = env.svc->login.login(test::kAlicePhone, "wrong", "10.0.0.1");
    CHECK(bad.status == LoginStatus::Failed);
    CHECK_FALSE(bad.session);

    const auto unknown = env.svc->login.login("+15555559999", "whatever", "10.0.0.1");
    CHECK(unknown.status == LoginStatus::Failed);
}

TEST_CASE("five wrong passwords lock the account for 15 minutes")
{
    test::Env env;
    for (int i = 0; i < 5; ++i)
        CHECK(env.svc->login.login(test::kAlicePhone, "guess-" + std::to_string(i), "10.0.0.2").status
              == LoginStatus::Failed);

    // 6th attempt with the CORRECT password is still refused.
    CHECK(env.svc->login.login(test::kAlicePhone, test::kAlicePassword, "10.0.0.2").status == LoginStatus::Failed);
    CHECK(env.saw_event("account_locked"));
    CHECK(env.saw_event("login_blocked_locked"));
    CHECK(env.count("SELECT EXTRACT(EPOCH FROM locked_until - now())::bigint FROM users WHERE phone = '+15555550101'")
          > 14 * 60);

    // Other accounts are unaffected.
    CHECK(env.svc->login.login(test::kBenPhone, test::kBenPassword, "10.0.0.2").status == LoginStatus::Success);

    // After the lock expires, the correct password works again.
    env.exec("UPDATE users SET locked_until = now() - interval '1 second' WHERE phone = '+15555550101'");
    CHECK(env.svc->login.login(test::kAlicePhone, test::kAlicePassword, "10.0.0.2").status == LoginStatus::Success);
}

TEST_CASE("too many login attempts from one IP are rate limited")
{
    test::Env env;
    int limited = 0;
    for (int i = 0; i < 25; ++i)
        if (env.svc->login.login("+1555555" + std::to_string(1000 + i), "x", "198.51.100.9").status
            == LoginStatus::RateLimited)
            ++limited;
    CHECK(limited == 5);  // 20 allowed per window
    CHECK(env.saw_event("login_rate_limited"));
    // A different IP is not affected.
    CHECK(env.svc->login.login(test::kAlicePhone, test::kAlicePassword, "198.51.100.10").status
          == LoginStatus::Success);
}

TEST_CASE("only a hash of the session token is stored")
{
    test::Env env;
    const auto s = env.svc->sessions.create(env.alice);
    CHECK(env.count("SELECT count(*) FROM sessions WHERE token_hash = '" + crypto::sha256_hex(s.token) + "'") == 1);
    CHECK(env.count("SELECT count(*) FROM sessions WHERE token_hash = '" + s.token + "'") == 0);
    CHECK(env.count("SELECT count(*) FROM sessions WHERE csrf_token = '" + s.token + "'") == 0);
}

TEST_CASE("sessions expire after 30 minutes idle and end on logout")
{
    test::Env env;
    const auto s = env.svc->sessions.create(env.alice);
    REQUIRE(env.svc->sessions.lookup(s.token));

    env.exec("UPDATE sessions SET last_seen = now() - interval '29 minutes'");
    CHECK(env.svc->sessions.lookup(s.token));  // activity refreshes last_seen

    env.exec("UPDATE sessions SET last_seen = now() - interval '31 minutes'");
    CHECK_FALSE(env.svc->sessions.lookup(s.token));
    CHECK(env.count("SELECT count(*) FROM sessions") == 0);
    CHECK(env.saw_event("session_expired"));

    const auto s2 = env.svc->sessions.create(env.alice);
    env.svc->sessions.destroy(s2.token);
    CHECK_FALSE(env.svc->sessions.lookup(s2.token));
    CHECK_FALSE(env.svc->sessions.lookup("forged-token"));
}

TEST_CASE("SQL injection strings are treated as plain data")
{
    test::Env env;
    for (const char* payload : {"' OR '1'='1", "' OR 1=1 --", "x'; DROP TABLE users; --", "\" OR \"\"=\""}) {
        INFO("payload: ", payload);
        CHECK(env.svc->login.login(payload, payload, "10.0.0.3").status == LoginStatus::Failed);
        CHECK(env.svc->login.login(test::kAlicePhone, payload, "10.0.0.3").status == LoginStatus::Failed);
        CHECK_FALSE(env.svc->authorizations.find_recipient(std::string(test::kAlicePhone) + payload));
        CHECK_FALSE(env.svc->authorizations.find_recipient(payload));
    }
    CHECK(env.count("SELECT count(*) FROM users") == 5);  // table intact
}
