#include "payments/transfer.h"

#include <algorithm>
#include <stdexcept>

#include "money.h"
#include "security_log.h"
#include "validate.h"

namespace payments {

const char* to_string(ConfirmStatus s)
{
    switch (s) {
        case ConfirmStatus::Completed: return "completed";
        case ConfirmStatus::AlreadyCompleted: return "already_completed";
        case ConfirmStatus::NotFound: return "not_found";
        case ConfirmStatus::SessionMismatch: return "session_mismatch";
        case ConfirmStatus::Expired: return "expired";
        case ConfirmStatus::InsufficientFunds: return "insufficient_funds";
    }
    return "unknown";
}

ConfirmResult TransferService::confirm(std::string_view authorization_id, std::int64_t payer_id,
                                       const std::string& session_hash, const TransferHooks& hooks)
{
    const std::string uid = std::to_string(payer_id);
    if (!validate::is_uuid(authorization_id)) return {ConfirmStatus::NotFound};
    const std::string auth_id(authorization_id);

    auto conn = pool_.acquire();
    pqxx::work tx(*conn);  // rolls back automatically unless commit() is reached

    // 1. Lock the authorization row. Concurrent confirms of the same
    //    authorization queue here and see status = 'used' once the first commits.
    auto auth_rows = db::exec(tx,
                              "SELECT payee_id, amount_cents, kind, status, session_hash, expires_at <= now() "
                              "FROM payment_authorizations WHERE id = $1::uuid AND payer_id = $2 FOR UPDATE",
                              auth_id, payer_id);
    if (auth_rows.empty()) {
        seclog::event("payment_rejected", {{"reason", "not_found"}, {"user_id", uid}});
        return {ConfirmStatus::NotFound};
    }
    const auto& a = auth_rows[0];
    const auto payee_id = a[0].as<std::int64_t>();
    const auto amount = a[1].as<std::int64_t>();
    const auto kind = a[2].as<std::string>();
    const auto status = a[3].as<std::string>();
    const bool expired = a[5].as<bool>();

    if (a[4].as<std::string>() != session_hash) {
        seclog::event("payment_rejected", {{"reason", "session_mismatch"}, {"user_id", uid}, {"auth", auth_id}});
        return {ConfirmStatus::SessionMismatch};
    }

    if (status == "used") {
        auto tx_row = db::exec(tx, "SELECT id FROM transactions WHERE authorization_id = $1::uuid", auth_id).one_row();
        seclog::event("payment_replay_blocked", {{"user_id", uid}, {"auth", auth_id}});
        return {ConfirmStatus::AlreadyCompleted, tx_row[0].as<std::int64_t>()};
    }

    if (status == "expired" || expired) {
        db::exec(tx, "UPDATE payment_authorizations SET status = 'expired' WHERE id = $1::uuid AND status = 'pending'",
                 auth_id);
        tx.commit();
        seclog::event("payment_rejected", {{"reason", "expired"}, {"user_id", uid}, {"auth", auth_id}});
        return {ConfirmStatus::Expired};
    }

    // 2. Lock both accounts in a fixed (ascending id) order so two opposite
    //    transfers can never deadlock.
    const auto first = std::min(payer_id, payee_id);
    const auto second = std::max(payer_id, payee_id);
    auto accounts = db::exec(tx,
                             "SELECT id, balance_cents FROM users WHERE id IN ($1, $2) ORDER BY id FOR UPDATE",
                             first, second);
    if (accounts.size() != 2) throw std::runtime_error("payment account missing");

    std::int64_t payer_balance = 0;
    for (const auto& row : accounts)
        if (row[0].as<std::int64_t>() == payer_id) payer_balance = row[1].as<std::int64_t>();

    // 3. Check funds against the locked, current balance.
    if (payer_balance < amount) {
        seclog::event("payment_rejected", {{"reason", "insufficient_funds"}, {"user_id", uid}, {"auth", auth_id}});
        return {ConfirmStatus::InsufficientFunds};
    }

    // 4. Move the money, record it, and burn the authorization -- all or nothing.
    db::exec(tx, "UPDATE users SET balance_cents = balance_cents - $2 WHERE id = $1", payer_id, amount);
    if (hooks.after_debit) hooks.after_debit();
    db::exec(tx, "UPDATE users SET balance_cents = balance_cents + $2 WHERE id = $1", payee_id, amount);
    auto txn = db::exec(tx,
                        "INSERT INTO transactions (authorization_id, payer_id, payee_id, amount_cents, kind) "
                        "VALUES ($1::uuid, $2, $3, $4, $5) RETURNING id",
                        auth_id, payer_id, payee_id, amount, kind)
                   .one_row();
    db::exec(tx, "UPDATE payment_authorizations SET status = 'used', used_at = now() WHERE id = $1::uuid", auth_id);
    tx.commit();

    const auto transaction_id = txn[0].as<std::int64_t>();
    seclog::event("payment_completed", {{"user_id", uid},
                                        {"payee_id", std::to_string(payee_id)},
                                        {"amount", money::format_cents(amount)},
                                        {"transaction_id", std::to_string(transaction_id)}});
    return {ConfirmStatus::Completed, transaction_id};
}

} // namespace payments
