#include "payments/authorization.h"

#include <stdexcept>

#include "money.h"
#include "validate.h"

namespace payments {

const char* to_string(Kind k) { return k == Kind::Qr ? "qr" : "phone"; }

std::optional<Merchant> AuthorizationService::find_merchant(std::int64_t merchant_id)
{
    auto conn = pool_.acquire();
    pqxx::read_transaction tx(*conn);
    auto rows = db::exec(tx, "SELECT id, user_id, display_name FROM merchants WHERE id = $1 AND active", merchant_id);
    if (rows.empty()) return std::nullopt;
    return Merchant{rows[0][0].as<std::int64_t>(), rows[0][1].as<std::int64_t>(), rows[0][2].as<std::string>()};
}

std::optional<Merchant> AuthorizationService::find_merchant_by_user(std::int64_t user_id)
{
    auto conn = pool_.acquire();
    pqxx::read_transaction tx(*conn);
    auto rows = db::exec(tx, "SELECT id, user_id, display_name FROM merchants WHERE user_id = $1 AND active", user_id);
    if (rows.empty()) return std::nullopt;
    return Merchant{rows[0][0].as<std::int64_t>(), rows[0][1].as<std::int64_t>(), rows[0][2].as<std::string>()};
}

std::optional<Recipient> AuthorizationService::find_recipient(const std::string& phone)
{
    auto conn = pool_.acquire();
    pqxx::read_transaction tx(*conn);
    auto rows = db::exec(tx, "SELECT id, full_name, phone FROM users WHERE phone = $1", phone);
    if (rows.empty()) return std::nullopt;
    return Recipient{rows[0][0].as<std::int64_t>(), validate::mask_name(rows[0][1].as<std::string>()),
                     validate::mask_phone(rows[0][2].as<std::string>())};
}

std::string AuthorizationService::create(std::int64_t payer_id, std::int64_t payee_id, std::int64_t amount_cents,
                                         Kind kind, const std::string& session_hash)
{
    if (payer_id == payee_id) throw std::invalid_argument("payer and payee must differ");
    if (amount_cents < money::kMinPaymentCents || amount_cents > money::kMaxPaymentCents)
        throw std::invalid_argument("amount out of range");

    auto conn = pool_.acquire();
    pqxx::work tx(*conn);
    auto row = db::exec(tx,
                        "INSERT INTO payment_authorizations "
                        "  (payer_id, payee_id, amount_cents, kind, session_hash, expires_at) "
                        "VALUES ($1, $2, $3, $4, $5, now() + make_interval(secs => $6)) RETURNING id",
                        payer_id, payee_id, amount_cents, to_string(kind), session_hash,
                        static_cast<std::int64_t>(kAuthorizationLifetime.count()))
                   .one_row();
    tx.commit();
    return row[0].as<std::string>();
}

std::optional<Authorization> AuthorizationService::load_for(std::string_view id, std::int64_t payer_id,
                                                            const std::string& session_hash)
{
    if (!validate::is_uuid(id)) return std::nullopt;
    auto conn = pool_.acquire();
    pqxx::read_transaction tx(*conn);
    auto rows = db::exec(tx,
                         "SELECT a.id, a.payer_id, a.payee_id, a.amount_cents, a.kind, a.status, "
                         "       m.display_name, u.full_name, "
                         "       GREATEST(0, CEIL(EXTRACT(EPOCH FROM a.expires_at - now())))::bigint, "
                         "       a.expires_at <= now(), COALESCE(t.id, 0) "
                         "FROM payment_authorizations a "
                         "JOIN users u ON u.id = a.payee_id "
                         "LEFT JOIN merchants m ON m.user_id = a.payee_id "
                         "LEFT JOIN transactions t ON t.authorization_id = a.id "
                         "WHERE a.id = $1::uuid AND a.payer_id = $2 AND a.session_hash = $3",
                         std::string(id), payer_id, session_hash);
    if (rows.empty()) return std::nullopt;
    const auto& r = rows[0];
    Authorization a;
    a.id = r[0].as<std::string>();
    a.payer_id = r[1].as<std::int64_t>();
    a.payee_id = r[2].as<std::int64_t>();
    a.amount_cents = r[3].as<std::int64_t>();
    a.kind = r[4].as<std::string>();
    a.status = r[5].as<std::string>();
    a.payee_display = (a.kind == "qr" && !r[6].is_null()) ? r[6].as<std::string>()
                                                           : validate::mask_name(r[7].as<std::string>());
    a.seconds_left = r[8].as<std::int64_t>();
    a.expired = r[9].as<bool>();
    a.transaction_id = r[10].as<std::int64_t>();
    return a;
}

} // namespace payments
