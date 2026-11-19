#include "fixture.h"

#include <cctype>
#include <cstdlib>

#include "auth/password.h"
#include "security_log.h"

namespace test {
namespace {

config::Config test_config()
{
    config::Config cfg;
    cfg.database_url = database_url();
    cfg.qr_key.fill(0x42);
    cfg.template_dir = "templates";
    cfg.db_pool_size = 24;
    cfg.secure_cookies = true;
    return cfg;
}

// Hashing is deliberately slow, so hash each test password once.
const std::string& hash_of(const std::string& password)
{
    static std::map<std::string, std::string> cache;
    static std::mutex m;
    std::lock_guard lock(m);
    auto it = cache.find(password);
    if (it == cache.end()) it = cache.emplace(password, auth::hash_password(password)).first;
    return it->second;
}

std::string url_encode(const std::string& s)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 0xF];
        }
    }
    return out;
}

} // namespace

std::string database_url()
{
    const char* url = std::getenv("SCANPAY_TEST_DATABASE_URL");
    return url ? url : "";
}

Env::Env() : admin(database_url())
{
    {
        pqxx::work tx(admin);
        tx.exec("TRUNCATE transactions, payment_authorizations, sessions, merchants, users RESTART IDENTITY CASCADE");
        auto add = [&](const char* name, const char* phone, const std::string& pw, const char* role,
                       std::int64_t cents) {
            return tx.exec("INSERT INTO users (full_name, phone, password_hash, role, balance_cents) "
                           "VALUES ($1, $2, $3, $4, $5) RETURNING id",
                           pqxx::params{name, phone, hash_of(pw), role, cents})
                .one_row()[0]
                .as<std::int64_t>();
        };
        alice = add("Alice Carter", kAlicePhone, kAlicePassword, "customer", 25'000);
        ben = add("Ben Ortiz", kBenPhone, kBenPassword, "customer", 10'000);
        chloe = add("Chloe Nguyen", "+15555550103", "chloe-test-pw", "customer", 4'000);
        coffee_user = add("Golden Eagle Coffee", "+15555550201", "coffee-test-pw", "merchant", 0);
        books_user = add("Hattiesburg Books", "+15555550202", "books-test-pw", "merchant", 0);
        coffee_merchant = tx.exec("INSERT INTO merchants (user_id, display_name) VALUES ($1, $2) RETURNING id",
                                  pqxx::params{coffee_user, "Golden Eagle Coffee"})
                              .one_row()[0]
                              .as<std::int64_t>();
        books_merchant = tx.exec("INSERT INTO merchants (user_id, display_name) VALUES ($1, $2) RETURNING id",
                                 pqxx::params{books_user, "Hattiesburg Books"})
                             .one_row()[0]
                             .as<std::int64_t>();
        tx.commit();
    }
    seclog::set_writer([this](std::string_view line) {
        std::lock_guard lock(events_mutex_);
        events_.emplace_back(line);
    });
    svc = std::make_unique<web::Services>(test_config());
}

Env::~Env()
{
    seclog::set_writer([](std::string_view) {});
}

std::int64_t Env::balance(std::int64_t user_id)
{
    pqxx::read_transaction tx(admin);
    return tx.exec("SELECT balance_cents FROM users WHERE id = $1", pqxx::params{user_id})
        .one_row()[0]
        .as<std::int64_t>();
}

std::int64_t Env::count(const std::string& sql)
{
    pqxx::read_transaction tx(admin);
    return tx.exec(sql).one_row()[0].as<std::int64_t>();
}

void Env::exec(const std::string& sql)
{
    pqxx::work tx(admin);
    tx.exec(sql);
    tx.commit();
}

std::vector<std::string> Env::events()
{
    std::lock_guard lock(events_mutex_);
    return events_;
}

bool Env::saw_event(const std::string& name)
{
    const std::string needle = "\"event\":\"" + name + "\"";
    for (const auto& e : events())
        if (e.find(needle) != std::string::npos) return true;
    return false;
}

// ---------------------------------------------------------------------------

std::string extract_input_value(const std::string& html, const std::string& name)
{
    const std::string marker = "name=\"" + name + "\" value=\"";
    auto pos = html.find(marker);
    if (pos == std::string::npos) return {};
    pos += marker.size();
    return html.substr(pos, html.find('"', pos) - pos);
}

Browser::Browser(web::Services& svc)
{
    web::register_routes(app_, svc);
    app_.validate();
}

Browser::Response Browser::send(crow::request& req)
{
    req.remote_ip_address = "203.0.113.7";
    std::string cookie_header;
    for (const auto& [k, v] : cookies_) {
        if (!cookie_header.empty()) cookie_header += "; ";
        cookie_header += k + "=" + v;
    }
    if (!cookie_header.empty()) req.headers.emplace("Cookie", cookie_header);

    crow::response res;
    app_.handle_full(req, res);

    Response out;
    out.code = res.code;
    out.body = res.body;
    out.location = res.get_header_value("Location");
    out.headers = res.headers;
    auto range = res.headers.equal_range("Set-Cookie");
    for (auto it = range.first; it != range.second; ++it) {
        out.set_cookies.push_back(it->second);
        const auto& c = it->second;
        const auto eq = c.find('=');
        const auto semi = c.find(';');
        const std::string name = c.substr(0, eq);
        const std::string value = c.substr(eq + 1, semi - eq - 1);
        if (c.find("Max-Age=0") != std::string::npos)
            cookies_.erase(name);
        else
            cookies_[name] = value;
    }
    if (auto token = extract_input_value(out.body, "csrf_token"); !token.empty()) csrf_ = token;
    return out;
}

Browser::Response Browser::get(const std::string& path)
{
    crow::request req;
    req.method = crow::HTTPMethod::Get;
    req.url = path;
    req.raw_url = path;
    return send(req);
}

Browser::Response Browser::post(const std::string& path, const std::map<std::string, std::string>& form)
{
    crow::request req;
    req.method = crow::HTTPMethod::Post;
    req.url = path;
    req.raw_url = path;
    std::string body;
    for (const auto& [k, v] : form) {
        if (!body.empty()) body += '&';
        body += url_encode(k) + "=" + url_encode(v);
    }
    req.body = body;
    req.headers.emplace("Content-Type", "application/x-www-form-urlencoded");
    return send(req);
}

Browser::Response Browser::login(const std::string& phone, const std::string& password)
{
    get("/login");
    return post("/login", {{"csrf_token", csrf_}, {"phone", phone}, {"password", password}});
}

} // namespace test
