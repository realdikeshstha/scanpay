#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "auth/session.h"
#include "crow.h"
#include "web/services.h"

namespace web {

inline constexpr std::size_t kMaxBodyBytes = 16 * 1024;

enum class Access {
    Public,    // no session required
    User,      // any logged-in user; POSTs must carry the session CSRF token
    Merchant,  // logged-in merchant accounts only
};

struct RequestContext {
    // cppcheck-suppress uninitMemberVarNoCtor ; aggregate, always brace-initialized
    const crow::request& req;
    Services& svc;
    std::optional<auth::Session> session;
    std::string ip;
    crow::query_string form;

    // Form field from a POST body, or "" if absent.
    std::string field(std::string_view name) const;
};

using Handler = std::function<crow::response(const RequestContext&)>;

// Wraps every route. In order: rejects oversized bodies, loads the session from
// its cookie, enforces login / role, verifies the CSRF token on state-changing
// requests, runs the handler, converts exceptions into a generic 500 page, and
// adds security headers.
crow::response guarded(Services& svc, const crow::request& req, Access access, const Handler& handler);

void apply_security_headers(crow::response& res, bool hsts);

// 303 See Other (POST -> redirect -> GET).
crow::response redirect(const std::string& location);

crow::mustache::context base_context(const RequestContext& ctx, const std::string& title);
crow::response render(const RequestContext& ctx, int code, const std::string& view,
                      const crow::mustache::context& data);
crow::response error_page(const RequestContext& ctx, int code, const std::string& title, const std::string& message);

// Crow middleware so that static files and Crow-generated errors also carry
// the security headers.
struct SecurityHeaders {
    struct context {};
    bool hsts = false;
    // cppcheck-suppress functionStatic ; Crow requires a member function
    void before_handle(crow::request&, crow::response&, context&) {}
    void after_handle(crow::request&, crow::response& res, context&) { apply_security_headers(res, hsts); }
};

} // namespace web
