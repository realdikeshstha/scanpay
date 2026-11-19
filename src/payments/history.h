#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "db/pool.h"

namespace payments {

// One transaction as seen by a specific viewer. When the viewer is the payee,
// the payer is shown only as a masked name and the last four phone digits.
struct TransactionView {
    std::int64_t id = 0;
    bool outgoing = false;
    std::string counterparty;
    std::int64_t amount_cents = 0;
    std::string kind;
    std::string created_at;
    std::string authorization_id;
};

class HistoryService {
public:
    explicit HistoryService(db::Pool& pool) : pool_(pool) {}

    std::int64_t balance(std::int64_t user_id);

    // Only transactions where the user is payer or payee.
    std::vector<TransactionView> list_for(std::int64_t user_id, int limit);

    // Ownership check is part of the query: other users' receipts do not exist.
    std::optional<TransactionView> receipt_for(std::int64_t transaction_id, std::int64_t user_id);

private:
    db::Pool& pool_;
};

} // namespace payments
