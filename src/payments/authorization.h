#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "db/pool.h"

namespace payments {

inline constexpr std::chrono::seconds kAuthorizationLifetime{120};

enum class Kind { Qr, Phone };

const char* to_string(Kind k);

struct Merchant {
    std::int64_t merchant_id = 0;
    std::int64_t user_id = 0;
    std::string display_name;
};

struct Recipient {
    std::int64_t user_id = 0;
    std::string masked_name;
    std::string masked_phone;
};

struct Authorization {
    std::string id;
    std::int64_t payer_id = 0;
    std::int64_t payee_id = 0;
    std::int64_t amount_cents = 0;
    std::string kind;
    std::string status;
    std::string payee_display;  // merchant name, or masked recipient name
    std::int64_t seconds_left = 0;
    bool expired = false;
    std::int64_t transaction_id = 0;  // set once the authorization is used
};

class AuthorizationService {
public:
    explicit AuthorizationService(db::Pool& pool) : pool_(pool) {}

    std::optional<Merchant> find_merchant(std::int64_t merchant_id);
    std::optional<Merchant> find_merchant_by_user(std::int64_t user_id);
    std::optional<Recipient> find_recipient(const std::string& phone);

    // Creates a pending authorization valid for two minutes. The amount stored
    // here is the only amount that will ever be debited, credited or shown on
    // the receipt.
    std::string create(std::int64_t payer_id, std::int64_t payee_id, std::int64_t amount_cents, Kind kind,
                       const std::string& session_hash);

    // Loads an authorization only if it belongs to this payer and session.
    std::optional<Authorization> load_for(std::string_view id, std::int64_t payer_id,
                                          const std::string& session_hash);

private:
    db::Pool& pool_;
};

} // namespace payments
