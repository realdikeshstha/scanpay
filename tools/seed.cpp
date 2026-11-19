// Inserts the fictional demo accounts (idempotent). Passwords are hashed with
// Argon2id here; only the hashes reach the database.
//
//   DATABASE_URL=... scanpay_seed
//
// Optional: SCANPAY_QR_KEY=... prints each merchant's signed QR payload.

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "auth/password.h"
#include "config.h"
#include "crypto.h"
#include "db/pool.h"
#include "qr/qr_token.h"

namespace {

struct DemoAccount {
    std::string name;
    std::string phone;
    std::string password;
    std::string role;
    std::int64_t balance_cents = 0;
    std::string merchant_name;  // empty for customers
};

// Fictional test data only. These demo passwords are documented in README.md.
const std::vector<DemoAccount> kAccounts = {
    {"Alice Carter", "+15555550101", "alice-demo-2026", "customer", 25'000, ""},
    {"Ben Ortiz", "+15555550102", "ben-demo-2026", "customer", 10'000, ""},
    {"Chloe Nguyen", "+15555550103", "chloe-demo-2026", "customer", 4'000, ""},
    {"Golden Eagle Coffee", "+15555550201", "coffee-demo-2026", "merchant", 0, "Golden Eagle Coffee"},
    {"Hattiesburg Books", "+15555550202", "books-demo-2026", "merchant", 0, "Hattiesburg Books"},
};

} // namespace

int main()
{
    try {
        crypto::init();
        const char* url = std::getenv("DATABASE_URL");
        if (!url || !*url) {
            std::cerr << "DATABASE_URL is required\n";
            return 2;
        }
        pqxx::connection conn(url);
        pqxx::work tx(conn);

        std::vector<std::pair<std::string, std::int64_t>> merchants;
        for (const auto& a : kAccounts) {
            auto existing = db::exec(tx, "SELECT id FROM users WHERE phone = $1", a.phone);
            std::int64_t user_id = 0;
            if (existing.empty()) {
                user_id = db::exec(tx,
                                   "INSERT INTO users (full_name, phone, password_hash, role, balance_cents) "
                                   "VALUES ($1, $2, $3, $4, $5) RETURNING id",
                                   a.name, a.phone, auth::hash_password(a.password), a.role, a.balance_cents)
                              .one_row()[0]
                              .as<std::int64_t>();
                std::cout << "created " << a.role << " " << a.name << " (" << a.phone << ")\n";
            } else {
                user_id = existing[0][0].as<std::int64_t>();
                std::cout << "exists  " << a.role << " " << a.name << " (" << a.phone << ")\n";
            }
            if (!a.merchant_name.empty()) {
                auto m = db::exec(tx, "SELECT id FROM merchants WHERE user_id = $1", user_id);
                std::int64_t merchant_id = m.empty() ? db::exec(tx,
                                                                "INSERT INTO merchants (user_id, display_name) "
                                                                "VALUES ($1, $2) RETURNING id",
                                                                user_id, a.merchant_name)
                                                           .one_row()[0]
                                                           .as<std::int64_t>()
                                                     : m[0][0].as<std::int64_t>();
                merchants.emplace_back(a.merchant_name, merchant_id);
            }
        }
        tx.commit();

        if (const char* key = std::getenv("SCANPAY_QR_KEY"); key && *key) {
            qr::Signer signer(config::parse_qr_key(key));
            std::cout << "\nMerchant QR payloads:\n";
            for (const auto& [name, id] : merchants) std::cout << "  " << name << ": " << signer.payload_for(id) << '\n';
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "seed failed: " << e.what() << '\n';
        return 1;
    }
}
