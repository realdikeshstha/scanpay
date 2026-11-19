// Payment integrity tests: one test per success criterion in the proposal.

#include <doctest/doctest.h>

#include <atomic>
#include <cstdlib>
#include <stdexcept>
#include <thread>
#include <vector>

#include "fixture.h"

using payments::ConfirmStatus;
using payments::Kind;

namespace {

struct Payer {
    std::int64_t user_id;
    std::string session_hash;
};

Payer session_for(test::Env& env, std::int64_t user_id)
{
    return {user_id, env.svc->sessions.create(user_id).session.token_hash};
}

} // namespace

TEST_CASE("a QR payment moves the exact amount from payer to merchant")
{
    test::Env env;
    auto alice = session_for(env, env.alice);
    const auto id = env.svc->authorizations.create(alice.user_id, env.coffee_user, 475, Kind::Qr, alice.session_hash);
    const auto r = env.svc->transfers.confirm(id, alice.user_id, alice.session_hash);
    REQUIRE(r.status == ConfirmStatus::Completed);
    CHECK(env.balance(env.alice) == 25'000 - 475);
    CHECK(env.balance(env.coffee_user) == 475);
    CHECK(env.count("SELECT count(*) FROM transactions") == 1);
    CHECK(env.count("SELECT count(*) FROM payment_authorizations WHERE status = 'used'") == 1);
}

TEST_CASE("submitting one authorization 20 times concurrently creates exactly one transaction")
{
    test::Env env;
    auto alice = session_for(env, env.alice);
    const auto id = env.svc->authorizations.create(alice.user_id, env.coffee_user, 1'000, Kind::Qr, alice.session_hash);

    std::atomic<int> completed{0}, replayed{0}, other{0};
    std::vector<std::int64_t> tx_ids(20);
    std::vector<std::thread> threads;
    for (int i = 0; i < 20; ++i) {
        threads.emplace_back([&, i] {
            const auto r = env.svc->transfers.confirm(id, alice.user_id, alice.session_hash);
            tx_ids[static_cast<std::size_t>(i)] = r.transaction_id;
            if (r.status == ConfirmStatus::Completed) ++completed;
            else if (r.status == ConfirmStatus::AlreadyCompleted) ++replayed;
            else ++other;
        });
    }
    for (auto& t : threads) t.join();

    CHECK(completed == 1);
    CHECK(replayed == 19);
    CHECK(other == 0);
    for (auto tx : tx_ids) CHECK(tx == tx_ids[0]);  // every caller gets the same receipt
    CHECK(env.count("SELECT count(*) FROM transactions") == 1);
    CHECK(env.balance(env.alice) == 25'000 - 1'000);
    CHECK(env.balance(env.coffee_user) == 1'000);
    CHECK(env.saw_event("payment_replay_blocked"));
}

TEST_CASE("concurrent payments cannot spend more than the balance")
{
    test::Env env;
    // Chloe has $40.00; 16 concurrent $5.00 payments = $80.00 requested.
    auto chloe = session_for(env, env.chloe);
    std::vector<std::string> ids;
    for (int i = 0; i < 16; ++i)
        ids.push_back(env.svc->authorizations.create(chloe.user_id, i % 2 ? env.coffee_user : env.books_user, 500,
                                                     Kind::Qr, chloe.session_hash));

    std::atomic<int> completed{0}, insufficient{0}, other{0};
    std::vector<std::thread> threads;
    for (const auto& id : ids) {
        threads.emplace_back([&, id] {
            const auto r = env.svc->transfers.confirm(id, chloe.user_id, chloe.session_hash);
            if (r.status == ConfirmStatus::Completed) ++completed;
            else if (r.status == ConfirmStatus::InsufficientFunds) ++insufficient;
            else ++other;
        });
    }
    for (auto& t : threads) t.join();

    CHECK(completed == 8);
    CHECK(insufficient == 8);
    CHECK(other == 0);
    CHECK(env.balance(env.chloe) == 0);
    CHECK(env.balance(env.coffee_user) + env.balance(env.books_user) == 4'000);
    CHECK(env.count("SELECT count(*) FROM transactions") == 8);
    // Money is conserved across all accounts.
    CHECK(env.count("SELECT sum(balance_cents) FROM users") == 25'000 + 10'000 + 4'000);
}

TEST_CASE("opposite transfers at the same time do not deadlock")
{
    test::Env env;
    auto alice = session_for(env, env.alice);
    auto ben = session_for(env, env.ben);
    std::vector<std::string> a_to_b, b_to_a;
    for (int i = 0; i < 10; ++i) {
        a_to_b.push_back(env.svc->authorizations.create(alice.user_id, env.ben, 100, Kind::Phone, alice.session_hash));
        b_to_a.push_back(env.svc->authorizations.create(ben.user_id, env.alice, 100, Kind::Phone, ben.session_hash));
    }
    std::atomic<int> completed{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&, i] {
            if (env.svc->transfers.confirm(a_to_b[static_cast<std::size_t>(i)], alice.user_id, alice.session_hash).status
                == ConfirmStatus::Completed)
                ++completed;
        });
        threads.emplace_back([&, i] {
            if (env.svc->transfers.confirm(b_to_a[static_cast<std::size_t>(i)], ben.user_id, ben.session_hash).status
                == ConfirmStatus::Completed)
                ++completed;
        });
    }
    for (auto& t : threads) t.join();
    CHECK(completed == 20);
    CHECK(env.balance(env.alice) == 25'000);
    CHECK(env.balance(env.ben) == 10'000);
}

TEST_CASE("a failure in the middle of a transfer rolls everything back")
{
    test::Env env;
    auto alice = session_for(env, env.alice);
    const auto id = env.svc->authorizations.create(alice.user_id, env.coffee_user, 2'000, Kind::Qr, alice.session_hash);

    payments::TransferHooks crash;
    crash.after_debit = [] { throw std::runtime_error("simulated crash after debit"); };
    CHECK_THROWS(env.svc->transfers.confirm(id, alice.user_id, alice.session_hash, crash));

    CHECK(env.balance(env.alice) == 25'000);
    CHECK(env.balance(env.coffee_user) == 0);
    CHECK(env.count("SELECT count(*) FROM transactions") == 0);
    CHECK(env.count("SELECT count(*) FROM payment_authorizations WHERE status = 'pending'") == 1);

    // The untouched authorization can still be completed normally.
    CHECK(env.svc->transfers.confirm(id, alice.user_id, alice.session_hash).status == ConfirmStatus::Completed);
    CHECK(env.balance(env.alice) == 23'000);
}

TEST_CASE("an expired authorization cannot be used")
{
    test::Env env;
    auto alice = session_for(env, env.alice);
    const auto id = env.svc->authorizations.create(alice.user_id, env.coffee_user, 300, Kind::Qr, alice.session_hash);
    env.exec("UPDATE payment_authorizations SET expires_at = now() - interval '1 second'");

    CHECK(env.svc->transfers.confirm(id, alice.user_id, alice.session_hash).status == ConfirmStatus::Expired);
    CHECK(env.count("SELECT count(*) FROM payment_authorizations WHERE status = 'expired'") == 1);
    CHECK(env.balance(env.alice) == 25'000);
    CHECK(env.count("SELECT count(*) FROM transactions") == 0);
}

TEST_CASE("authorizations default to a two minute lifetime")
{
    test::Env env;
    auto alice = session_for(env, env.alice);
    env.svc->authorizations.create(alice.user_id, env.coffee_user, 300, Kind::Qr, alice.session_hash);
    CHECK(env.count("SELECT EXTRACT(EPOCH FROM expires_at - created_at)::bigint FROM payment_authorizations") == 120);
}

TEST_CASE("an authorization is bound to the session and payer that created it")
{
    test::Env env;
    auto alice = session_for(env, env.alice);
    auto alice_other_device = session_for(env, env.alice);
    auto ben = session_for(env, env.ben);
    const auto id = env.svc->authorizations.create(alice.user_id, env.coffee_user, 300, Kind::Qr, alice.session_hash);

    CHECK(env.svc->transfers.confirm(id, alice.user_id, alice_other_device.session_hash).status
          == ConfirmStatus::SessionMismatch);
    CHECK(env.svc->transfers.confirm(id, ben.user_id, ben.session_hash).status == ConfirmStatus::NotFound);
    CHECK(env.svc->transfers.confirm("not-a-uuid", alice.user_id, alice.session_hash).status == ConfirmStatus::NotFound);
    CHECK(env.balance(env.alice) == 25'000);
}

TEST_CASE("the database itself rejects negative balances and out-of-range amounts")
{
    test::Env env;
    CHECK_THROWS_AS(env.exec("UPDATE users SET balance_cents = -1 WHERE phone = '+15555550101'"), pqxx::check_violation);
    CHECK_THROWS_AS(env.exec("INSERT INTO payment_authorizations (payer_id, payee_id, session_hash, amount_cents, kind, "
                             "expires_at) VALUES (1, 4, 'x', 50001, 'qr', now())"),
                    pqxx::check_violation);
    CHECK_THROWS_AS(env.exec("UPDATE users SET password_hash = 'plaintext' WHERE id = 1"), pqxx::check_violation);
}

TEST_CASE("payments cannot be created for out-of-range amounts or to yourself")
{
    test::Env env;
    auto alice = session_for(env, env.alice);
    CHECK_THROWS(env.svc->authorizations.create(alice.user_id, env.coffee_user, 0, Kind::Qr, alice.session_hash));
    CHECK_THROWS(env.svc->authorizations.create(alice.user_id, env.coffee_user, 50'001, Kind::Qr, alice.session_hash));
    CHECK_THROWS(env.svc->authorizations.create(alice.user_id, alice.user_id, 100, Kind::Phone, alice.session_hash));
}

TEST_CASE("the application database role cannot rewrite the ledger" * doctest::skip(!std::getenv("SCANPAY_TEST_APP_DATABASE_URL")))
{
    test::Env env;
    pqxx::connection app(std::getenv("SCANPAY_TEST_APP_DATABASE_URL"));
    auto attempt = [&](const char* sql) {
        pqxx::work tx(app);
        tx.exec(sql);
        tx.commit();
    };
    CHECK_THROWS_AS(attempt("DELETE FROM transactions"), pqxx::insufficient_privilege);
    CHECK_THROWS_AS(attempt("DELETE FROM users"), pqxx::insufficient_privilege);
    CHECK_THROWS_AS(attempt("DROP TABLE transactions"), pqxx::sql_error);
    CHECK_THROWS_AS(attempt("TRUNCATE payment_authorizations"), pqxx::insufficient_privilege);
}
