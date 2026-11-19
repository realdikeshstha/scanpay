#include "payments/history.h"

#include "validate.h"

namespace payments {
namespace {

constexpr const char* kSelect =
    "SELECT t.id, t.payer_id = $1 AS outgoing, t.amount_cents, t.kind, "
    "       to_char(t.created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD HH24:MI \"UTC\"'), t.authorization_id::text, "
    "       pu.full_name, pu.phone, ru.full_name, rm.display_name "
    "FROM transactions t "
    "JOIN users pu ON pu.id = t.payer_id "
    "JOIN users ru ON ru.id = t.payee_id "
    "LEFT JOIN merchants rm ON rm.user_id = t.payee_id ";

TransactionView to_view(const pqxx::result& rows, pqxx::result::size_type i)
{
    const auto r = rows[i];
    TransactionView v;
    v.id = r[0].as<std::int64_t>();
    v.outgoing = r[1].as<bool>();
    v.amount_cents = r[2].as<std::int64_t>();
    v.kind = r[3].as<std::string>();
    v.created_at = r[4].as<std::string>();
    v.authorization_id = r[5].as<std::string>();
    if (v.outgoing) {
        // Viewer paid: show the merchant name, or the masked recipient name.
        v.counterparty = r[9].is_null() ? validate::mask_name(r[8].as<std::string>()) : r[9].as<std::string>();
    } else {
        // Viewer received: never reveal the payer's full name or phone.
        v.counterparty = validate::mask_name(r[6].as<std::string>()) + " (" +
                         validate::mask_phone(r[7].as<std::string>()) + ")";
    }
    return v;
}

} // namespace

std::int64_t HistoryService::balance(std::int64_t user_id)
{
    auto conn = pool_.acquire();
    pqxx::read_transaction tx(*conn);
    return db::exec(tx, "SELECT balance_cents FROM users WHERE id = $1", user_id).one_row()[0].as<std::int64_t>();
}

std::vector<TransactionView> HistoryService::list_for(std::int64_t user_id, int limit)
{
    auto conn = pool_.acquire();
    pqxx::read_transaction tx(*conn);
    const std::string sql = std::string(kSelect) +
                            "WHERE t.payer_id = $1 OR t.payee_id = $1 ORDER BY t.created_at DESC, t.id DESC LIMIT $2";
    auto rows = db::exec(tx, sql, user_id, limit);
    std::vector<TransactionView> out;
    out.reserve(rows.size());
    for (pqxx::result::size_type i = 0; i < rows.size(); ++i) out.push_back(to_view(rows, i));
    return out;
}

std::optional<TransactionView> HistoryService::receipt_for(std::int64_t transaction_id, std::int64_t user_id)
{
    auto conn = pool_.acquire();
    pqxx::read_transaction tx(*conn);
    const std::string sql = std::string(kSelect) + "WHERE t.id = $2 AND (t.payer_id = $1 OR t.payee_id = $1)";
    auto rows = db::exec(tx, sql, user_id, transaction_id);
    if (rows.empty()) return std::nullopt;
    return to_view(rows, 0);
}

} // namespace payments
