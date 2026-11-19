#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "db/pool.h"

namespace payments {

enum class ConfirmStatus {
    Completed,
    AlreadyCompleted,  // replayed confirmation; returns the original transaction
    NotFound,
    SessionMismatch,
    Expired,
    InsufficientFunds,
};

const char* to_string(ConfirmStatus s);

struct ConfirmResult {
    ConfirmStatus status = ConfirmStatus::NotFound;
    std::int64_t transaction_id = 0;
};

// Test-only fault injection points.
struct TransferHooks {
    std::function<void()> after_debit;
};

class TransferService {
public:
    explicit TransferService(db::Pool& pool) : pool_(pool) {}

    // Executes the authorization as one database transaction: lock the
    // authorization, lock both accounts (in id order), check funds, debit,
    // credit, record the transaction and mark the authorization used. Any
    // failure rolls everything back.
    ConfirmResult confirm(std::string_view authorization_id, std::int64_t payer_id, const std::string& session_hash,
                          const TransferHooks& hooks = {});

private:
    db::Pool& pool_;
};

} // namespace payments
