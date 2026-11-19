// End-to-end tests through the Crow router: CSRF, access control, XSS output
// encoding, QR tampering, input validation and security headers.

#include <doctest/doctest.h>

#include "fixture.h"

namespace {

bool contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

// Logs in and runs a full QR payment; returns the receipt path.
std::string pay_coffee(test::Env& env, test::Browser& b, const std::string& amount)
{
    const auto qr = env.svc->signer.payload_for(env.coffee_merchant);
    auto review = b.post("/pay/review", {{"csrf_token", b.csrf()}, {"qr", qr}});
    REQUIRE(review.code == 200);
    auto auth = b.post("/pay/authorize", {{"csrf_token", b.csrf()}, {"qr", qr}, {"amount", amount}});
    REQUIRE(auth.code == 303);
    auto confirm_page = b.get(auth.location);
    REQUIRE(confirm_page.code == 200);
    const auto auth_id = test::extract_input_value(confirm_page.body, "authorization_id");
    auto done = b.post("/pay/confirm", {{"csrf_token", b.csrf()}, {"authorization_id", auth_id}});
    REQUIRE(done.code == 303);
    return done.location;
}

} // namespace

TEST_CASE("protected pages redirect to login without a session")
{
    test::Env env;
    test::Browser b(*env.svc);
    for (const char* path : {"/", "/history", "/scan", "/transfer", "/receipts/1", "/merchant/qr"}) {
        INFO("path: ", path);
        const auto r = b.get(path);
        CHECK(r.code == 303);
        CHECK(r.location == "/login");
    }
}

TEST_CASE("login sets a hardened session cookie")
{
    test::Env env;
    test::Browser b(*env.svc);
    const auto r = b.login(test::kAlicePhone, test::kAlicePassword);
    REQUIRE(r.code == 303);
    CHECK(r.location == "/");
    bool found = false;
    for (const auto& c : r.set_cookies) {
        if (c.rfind("__Host-sp_session=", 0) != 0) continue;
        found = true;
        CHECK(contains(c, "HttpOnly"));
        CHECK(contains(c, "Secure"));
        CHECK(contains(c, "SameSite=Strict"));
        CHECK(contains(c, "Path=/"));
    }
    CHECK(found);
    CHECK(b.get("/").code == 200);
}

TEST_CASE("failed logins show one generic message")
{
    test::Env env;
    test::Browser b(*env.svc);
    const auto wrong_pw = b.login(test::kAlicePhone, "nope");
    const auto no_user = b.login("+15555559999", "nope");
    CHECK(wrong_pw.code == 401);
    CHECK(no_user.code == 401);
    CHECK(contains(wrong_pw.body, "Sign-in failed. Check your phone number and password."));
    CHECK(contains(no_user.body, "Sign-in failed. Check your phone number and password."));
}

TEST_CASE("login form requires its CSRF token")
{
    test::Env env;
    test::Browser b(*env.svc);
    b.get("/login");
    const auto r = b.post("/login", {{"phone", test::kAlicePhone}, {"password", test::kAlicePassword}});
    CHECK(r.code == 403);
    CHECK(env.saw_event("csrf_rejected"));
}

TEST_CASE("state-changing requests without the session CSRF token are blocked")
{
    test::Env env;
    test::Browser b(*env.svc);
    REQUIRE(b.login(test::kAlicePhone, test::kAlicePassword).code == 303);
    b.get("/");
    const auto qr = env.svc->signer.payload_for(env.coffee_merchant);

    CHECK(b.post("/pay/review", {{"qr", qr}}).code == 403);
    CHECK(b.post("/pay/review", {{"qr", qr}, {"csrf_token", "forged"}}).code == 403);
    CHECK(b.post("/transfer/authorize", {{"phone", test::kBenPhone}, {"amount", "5"}}).code == 403);
    CHECK(b.post("/logout", {}).code == 403);
    CHECK(env.saw_event("csrf_rejected"));
    CHECK(env.count("SELECT count(*) FROM payment_authorizations") == 0);

    CHECK(b.post("/pay/review", {{"qr", qr}, {"csrf_token", b.csrf()}}).code == 200);
}

TEST_CASE("full QR payment over HTTP, and replayed confirms return the same receipt")
{
    test::Env env;
    test::Browser b(*env.svc);
    REQUIRE(b.login(test::kAlicePhone, test::kAlicePassword).code == 303);
    b.get("/");
    const auto receipt = pay_coffee(env, b, "4.75");
    CHECK(receipt.rfind("/receipts/", 0) == 0);

    const auto page = b.get(receipt);
    CHECK(page.code == 200);
    CHECK(contains(page.body, "$4.75"));
    CHECK(contains(page.body, "Golden Eagle Coffee"));
    CHECK(env.balance(env.alice) == 25'000 - 475);

    const auto auth_id = env.count("SELECT count(*) FROM payment_authorizations");
    CHECK(auth_id == 1);
    pqxx::read_transaction tx(env.admin);
    const auto id = tx.exec("SELECT id::text FROM payment_authorizations").one_row()[0].as<std::string>();
    tx.commit();
    for (int i = 0; i < 5; ++i) {
        const auto again = b.post("/pay/confirm", {{"csrf_token", b.csrf()}, {"authorization_id", id}});
        CHECK(again.code == 303);
        CHECK(again.location == receipt);
    }
    CHECK(env.count("SELECT count(*) FROM transactions") == 1);
}

TEST_CASE("phone transfer shows only the masked recipient and moves the money")
{
    test::Env env;
    test::Browser b(*env.svc);
    REQUIRE(b.login(test::kAlicePhone, test::kAlicePassword).code == 303);
    b.get("/");
    const auto auth =
        b.post("/transfer/authorize", {{"csrf_token", b.csrf()}, {"phone", "+1 (555) 555-0102"}, {"amount", "12.50"}});
    REQUIRE(auth.code == 303);
    const auto confirm = b.get(auth.location);
    CHECK(contains(confirm.body, "Ben O."));
    CHECK_FALSE(contains(confirm.body, "Ortiz"));
    const auto id = test::extract_input_value(confirm.body, "authorization_id");
    CHECK(b.post("/pay/confirm", {{"csrf_token", b.csrf()}, {"authorization_id", id}}).code == 303);
    CHECK(env.balance(env.ben) == 10'000 + 1'250);
}

TEST_CASE("users cannot read other users' receipts")
{
    test::Env env;
    test::Browser alice(*env.svc);
    REQUIRE(alice.login(test::kAlicePhone, test::kAlicePassword).code == 303);
    alice.get("/");
    const auto receipt = pay_coffee(env, alice, "3.00");

    test::Browser ben(*env.svc);
    REQUIRE(ben.login(test::kBenPhone, test::kBenPassword).code == 303);
    const auto r = ben.get(receipt);
    CHECK(r.code == 404);
    CHECK_FALSE(contains(r.body, "$3.00"));
    CHECK(env.saw_event("access_denied"));
    CHECK(ben.get("/receipts/999999").code == 404);  // same answer as "does not exist"

    // The merchant (payee) can see it, but never the payer's full name or phone.
    test::Browser coffee(*env.svc);
    REQUIRE(coffee.login("+15555550201", "coffee-test-pw").code == 303);
    const auto m = coffee.get(receipt);
    CHECK(m.code == 200);
    CHECK(contains(m.body, "Alice C."));
    CHECK_FALSE(contains(m.body, "Carter"));
    CHECK_FALSE(contains(m.body, test::kAlicePhone));
}

TEST_CASE("customers cannot reach merchant-only pages")
{
    test::Env env;
    test::Browser b(*env.svc);
    REQUIRE(b.login(test::kAlicePhone, test::kAlicePassword).code == 303);
    CHECK(b.get("/merchant/qr").code == 404);
    CHECK(b.get("/merchant/qr.svg").code == 404);
}

TEST_CASE("tampered, forged and URL QR codes are rejected")
{
    test::Env env;
    test::Browser b(*env.svc);
    REQUIRE(b.login(test::kAlicePhone, test::kAlicePassword).code == 303);
    b.get("/");

    std::string swapped = env.svc->signer.payload_for(env.coffee_merchant);
    swapped.replace(swapped.find(":1:"), 3, ":2:");  // point the coffee QR at another merchant
    for (const std::string& qr :
         {swapped, std::string("https://evil.example/pay"), std::string("scanpay:v1:1:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"),
          std::string("<script>alert(1)</script>")}) {
        INFO("qr: ", qr);
        const auto r = b.post("/pay/review", {{"csrf_token", b.csrf()}, {"qr", qr}});
        CHECK(r.code == 400);
        CHECK(contains(r.body, "not a valid ScanPay merchant code"));
        CHECK(b.post("/pay/authorize", {{"csrf_token", b.csrf()}, {"qr", qr}, {"amount", "1"}}).code == 400);
    }
    CHECK(env.saw_event("qr_rejected"));
    CHECK(env.count("SELECT count(*) FROM payment_authorizations") == 0);
}

TEST_CASE("script input is HTML-escaped on output")
{
    test::Env env;
    env.exec("UPDATE merchants SET display_name = '<img src=x onerror=alert(1)>' WHERE id = 1");
    test::Browser b(*env.svc);
    REQUIRE(b.login(test::kAlicePhone, test::kAlicePassword).code == 303);
    b.get("/");
    const auto qr = env.svc->signer.payload_for(env.coffee_merchant);

    const auto review = b.post("/pay/review", {{"csrf_token", b.csrf()}, {"qr", qr}});
    CHECK_FALSE(contains(review.body, "<img src=x"));
    CHECK(contains(review.body, "&lt;img src&#x3D;x onerror&#x3D;alert(1)&gt;"));

    const auto bad_amount = b.post("/pay/authorize", {{"csrf_token", b.csrf()},
                                                      {"qr", qr},
                                                      {"amount", "\"><script>alert(1)</script>"}});
    CHECK(bad_amount.code == 400);
    CHECK_FALSE(contains(bad_amount.body, "<script>alert(1)</script>"));
    CHECK(contains(bad_amount.body, "&lt;script&gt;"));

    const auto bad_phone = b.post("/transfer/authorize", {{"csrf_token", b.csrf()},
                                                         {"phone", "<svg onload=alert(1)>"},
                                                         {"amount", "1"}});
    CHECK(bad_phone.code == 400);
    CHECK_FALSE(contains(bad_phone.body, "<svg onload"));
}

TEST_CASE("malformed and oversized input is rejected cleanly")
{
    test::Env env;
    test::Browser b(*env.svc);
    REQUIRE(b.login(test::kAlicePhone, test::kAlicePassword).code == 303);
    b.get("/");
    const auto qr = env.svc->signer.payload_for(env.coffee_merchant);

    for (const char* amount : {"0", "-5", "1e3", "500.01", "0.001", "abc", ""}) {
        INFO("amount: ", amount);
        const auto r = b.post("/pay/authorize", {{"csrf_token", b.csrf()}, {"qr", qr}, {"amount", amount}});
        CHECK(r.code == 400);
        CHECK(contains(r.body, "between $0.01 and $500.00"));
    }
    CHECK(b.post("/transfer/authorize", {{"csrf_token", b.csrf()}, {"phone", "5555550102"}, {"amount", "1"}}).code
          == 400);
    CHECK(b.post("/transfer/authorize", {{"csrf_token", b.csrf()}, {"phone", "+15555559999"}, {"amount", "1"}}).code
          == 404);
    CHECK(b.get("/authorizations/not-a-uuid").code == 404);
    CHECK(b.get("/authorizations/00000000-0000-0000-0000-000000000000").code == 404);

    const auto huge = b.post("/pay/review", {{"csrf_token", b.csrf()}, {"qr", std::string(20'000, 'A')}});
    CHECK(huge.code == 413);
    CHECK(env.count("SELECT count(*) FROM payment_authorizations") == 0);
}

TEST_CASE("phone lookups are rate limited")
{
    test::Env env;
    test::Browser b(*env.svc);
    REQUIRE(b.login(test::kAlicePhone, test::kAlicePassword).code == 303);
    b.get("/");
    int limited = 0;
    for (int i = 0; i < 15; ++i) {
        const auto r = b.post("/transfer/authorize",
                              {{"csrf_token", b.csrf()}, {"phone", "+1555555" + std::to_string(2000 + i)}, {"amount", "1"}});
        if (r.code == 429) ++limited;
    }
    CHECK(limited == 5);
    CHECK(env.saw_event("lookup_rate_limited"));
}

TEST_CASE("logout invalidates the session server-side")
{
    test::Env env;
    test::Browser b(*env.svc);
    REQUIRE(b.login(test::kAlicePhone, test::kAlicePassword).code == 303);
    b.get("/");
    const auto stolen_cookies = b.cookies();
    CHECK(b.post("/logout", {{"csrf_token", b.csrf()}}).code == 303);

    test::Browser attacker(*env.svc);
    attacker.cookies() = stolen_cookies;  // replay the old cookie
    const auto r = attacker.get("/");
    CHECK(r.code == 303);
    CHECK(r.location == "/login");
}

TEST_CASE("responses carry security headers")
{
    test::Env env;
    test::Browser b(*env.svc);
    const auto r = b.get("/login");
    CHECK(contains(r.headers.find("Content-Security-Policy")->second, "default-src 'self'"));
    CHECK(contains(r.headers.find("Content-Security-Policy")->second, "frame-ancestors 'none'"));
    CHECK(r.headers.find("X-Content-Type-Options")->second == "nosniff");
    CHECK(r.headers.find("X-Frame-Options")->second == "DENY");
    CHECK(r.headers.find("Cache-Control")->second == "no-store");
}
