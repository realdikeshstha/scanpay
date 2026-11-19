#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <pqxx/pqxx>

#include "web/routes.h"
#include "web/services.h"

namespace test {

// Connection string for a database with db/schema.sql applied. The tests
// truncate it, so never point this at real data.
std::string database_url();

// Fresh database contents + fresh Services for every test case.
struct Env {
    Env();
    ~Env();

    std::unique_ptr<web::Services> svc;
    pqxx::connection admin;

    std::int64_t alice = 0, ben = 0, chloe = 0;        // customers
    std::int64_t coffee_user = 0, coffee_merchant = 0;  // merchant
    std::int64_t books_user = 0, books_merchant = 0;

    std::int64_t balance(std::int64_t user_id);
    std::int64_t count(const std::string& sql);
    void exec(const std::string& sql);

    // Security events captured during the test.
    std::vector<std::string> events();
    bool saw_event(const std::string& name);

private:
    std::mutex events_mutex_;
    std::vector<std::string> events_;
};

inline constexpr const char* kAlicePhone = "+15555550101";
inline constexpr const char* kAlicePassword = "alice-test-pw";
inline constexpr const char* kBenPhone = "+15555550102";
inline constexpr const char* kBenPassword = "ben-test-pw";

// Minimal in-process HTTP client: drives requests through Crow's router with
// handle_full() and keeps cookies between requests.
class Browser {
public:
    struct Response {
        int code = 0;
        std::string body;
        std::string location;
        std::vector<std::string> set_cookies;
        crow::ci_map headers;
    };

    explicit Browser(web::Services& svc);

    Response get(const std::string& path);
    Response post(const std::string& path, const std::map<std::string, std::string>& form);

    // GET /login, then POST credentials with the login CSRF token.
    Response login(const std::string& phone, const std::string& password);

    // CSRF token embedded in the last page that had one.
    const std::string& csrf() const { return csrf_; }
    std::map<std::string, std::string>& cookies() { return cookies_; }

private:
    Response send(crow::request& req);

    web::App app_;
    std::map<std::string, std::string> cookies_;
    std::string csrf_;
};

std::string extract_input_value(const std::string& html, const std::string& name);

} // namespace test
