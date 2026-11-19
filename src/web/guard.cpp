#include "web/guard.h"

#include <iostream>

#include "auth/csrf.h"
#include "security_log.h"
#include "web/cookies.h"

namespace web {

std::string RequestContext::field(std::string_view name) const
{
    const char* value = form.get(std::string(name));
    return value ? std::string(value) : std::string{};
}

void apply_security_headers(crow::response& res, bool hsts)
{
    res.set_header("Content-Security-Policy",
                   "default-src 'self'; img-src 'self' data: blob:; media-src 'self' blob:; object-src 'none'; "
                   "base-uri 'none'; form-action 'self'; frame-ancestors 'none'");
    res.set_header("X-Content-Type-Options", "nosniff");
    res.set_header("X-Frame-Options", "DENY");
    res.set_header("Referrer-Policy", "no-referrer");
    res.set_header("Permissions-Policy", "camera=(self), microphone=(), geolocation=()");
    res.set_header("Cross-Origin-Opener-Policy", "same-origin");
    if (res.get_header_value("Cache-Control").empty()) res.set_header("Cache-Control", "no-store");
    if (hsts) res.set_header("Strict-Transport-Security", "max-age=31536000");
}

crow::response redirect(const std::string& location)
{
    crow::response res(303);
    res.set_header("Location", location);
    return res;
}

crow::mustache::context base_context(const RequestContext& ctx, const std::string& title)
{
    crow::mustache::context c;
    c["title"] = title;
    c["logged_in"] = ctx.session.has_value();
    if (ctx.session) {
        c["user_name"] = ctx.session->full_name;
        c["csrf_token"] = ctx.session->csrf_token;
        c["is_merchant"] = ctx.session->is_merchant();
    }
    return c;
}

crow::response render(const RequestContext& ctx, int code, const std::string& view,
                      const crow::mustache::context& data)
{
    crow::response res(code, ctx.svc.views.render(view, data));
    res.set_header("Content-Type", "text/html; charset=utf-8");
    return res;
}

crow::response error_page(const RequestContext& ctx, int code, const std::string& title, const std::string& message)
{
    auto c = base_context(ctx, title);
    c["message"] = message;
    return render(ctx, code, "error.html", c);
}

crow::response guarded(Services& svc, const crow::request& req, Access access, const Handler& handler)
{
    RequestContext ctx{req, svc, std::nullopt, req.remote_ip_address, req.get_body_params()};
    crow::response res;
    try {
        const bool state_changing = req.method == crow::HTTPMethod::Post;

        if (req.body.size() > kMaxBodyBytes) {
            seclog::event("request_too_large", {{"ip", ctx.ip}, {"bytes", std::to_string(req.body.size())}});
            res = error_page(ctx, 413, "Request too large", "The submitted data was too large.");
        } else {
            if (auto token = get_cookie(req, svc.session_cookie_name())) ctx.session = svc.sessions.lookup(*token);

            if (access != Access::Public && !ctx.session) {
                res = redirect("/login");
            } else if (access == Access::Merchant && !ctx.session->is_merchant()) {
                seclog::event("access_denied",
                              {{"user_id", std::to_string(ctx.session->user_id)}, {"path", req.url}, {"ip", ctx.ip}});
                res = error_page(ctx, 404, "Not found", "That page does not exist.");
            } else if (access != Access::Public && state_changing
                       && !auth::csrf_matches(ctx.session->csrf_token, ctx.field(auth::kCsrfField))) {
                seclog::event("csrf_rejected",
                              {{"user_id", std::to_string(ctx.session->user_id)}, {"path", req.url}, {"ip", ctx.ip}});
                res = error_page(ctx, 403, "Request blocked",
                                 "This form has expired or did not come from ScanPay. Please go back and try again.");
            } else {
                res = handler(ctx);
            }
        }
    } catch (const std::exception& e) {
        // Details stay in the server log; the client sees a generic page.
        std::cerr << "[error] " << req.url << ": " << e.what() << '\n';
        seclog::event("internal_error", {{"path", req.url}});
        try {
            res = error_page(ctx, 500, "Something went wrong", "The request could not be completed. Nothing was charged.");
        } catch (...) {
            res = crow::response(500, "Internal error");
        }
    }
    apply_security_headers(res, !svc.cfg.tls_cert_path.empty());
    return res;
}

} // namespace web
