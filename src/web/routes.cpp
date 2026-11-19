#include "web/routes.h"

#include "auth/csrf.h"
#include "crypto.h"
#include "money.h"
#include "qr/qr_svg.h"
#include "security_log.h"
#include "validate.h"
#include "web/cookies.h"

namespace web {
namespace {

using crow::HTTPMethod;

constexpr int kLoginCsrfMaxAge = 600;
constexpr int kDashboardItems = 5;
constexpr int kHistoryItems = 50;
const std::string kAmountError = "Enter an amount between $0.01 and $500.00 with at most two decimal places.";
const std::string kBadQrMessage =
    "This QR code is not a valid ScanPay merchant code. It may have been altered or replaced. Do not pay.";

std::string uid(const RequestContext& ctx) { return std::to_string(ctx.session->user_id); }

crow::json::wvalue transaction_item(const payments::TransactionView& t)
{
    crow::json::wvalue v;
    v["id"] = t.id;
    v["outgoing"] = t.outgoing;
    v["counterparty"] = t.counterparty;
    v["amount"] = (t.outgoing ? "-" : "+") + money::format_cents(t.amount_cents);
    v["kind"] = t.kind == "qr" ? "QR payment" : "Phone transfer";
    v["created_at"] = t.created_at;
    return v;
}

void add_transactions(crow::mustache::context& c, const std::vector<payments::TransactionView>& list)
{
    crow::json::wvalue::list items;
    for (const auto& t : list) items.push_back(transaction_item(t));
    c["has_transactions"] = !items.empty();
    c["transactions"] = std::move(items);
}

// ----- Login / logout -------------------------------------------------------

crow::response login_page(const RequestContext& ctx, int code, const std::string& login_csrf, const std::string& error)
{
    auto c = base_context(ctx, "Sign in");
    c["login_csrf"] = login_csrf;
    if (!error.empty()) c["error"] = error;
    c["phone"] = ctx.field("phone");
    auto res = render(ctx, code, "login.html", c);
    res.add_header("Set-Cookie", make_cookie(ctx.svc.login_csrf_cookie_name(), login_csrf,
                                             {ctx.svc.cfg.secure_cookies, kLoginCsrfMaxAge}));
    return res;
}

crow::response get_login(const RequestContext& ctx)
{
    if (ctx.session) return redirect("/");
    return login_page(ctx, 200, crypto::random_token(), "");
}

crow::response post_login(const RequestContext& ctx)
{
    // Login happens before a session exists, so CSRF uses a double-submit
    // cookie: the form token must match the HttpOnly, SameSite=Strict cookie.
    const auto cookie_token = get_cookie(ctx.req, ctx.svc.login_csrf_cookie_name());
    if (!cookie_token || !auth::csrf_matches(*cookie_token, ctx.field(auth::kCsrfField))) {
        seclog::event("csrf_rejected", {{"path", "/login"}, {"ip", ctx.ip}});
        return login_page(ctx, 403, crypto::random_token(), "Your sign-in form expired. Please try again.");
    }

    const auto result = ctx.svc.login.login(ctx.field("phone"), ctx.field("password"), ctx.ip);
    if (result.status == auth::LoginStatus::RateLimited)
        return login_page(ctx, 429, *cookie_token, "Too many sign-in attempts. Wait a few minutes and try again.");
    if (result.status != auth::LoginStatus::Success)
        return login_page(ctx, 401, *cookie_token,
                          "Sign-in failed. Check your phone number and password. "
                          "Repeated failures temporarily lock the account.");

    // Replace any previous session (prevents session fixation).
    if (auto old = get_cookie(ctx.req, ctx.svc.session_cookie_name())) ctx.svc.sessions.destroy(*old);

    auto res = redirect("/");
    res.add_header("Set-Cookie",
                   make_cookie(ctx.svc.session_cookie_name(), result.session->token, {ctx.svc.cfg.secure_cookies}));
    res.add_header("Set-Cookie",
                   make_cookie(ctx.svc.login_csrf_cookie_name(), "", {ctx.svc.cfg.secure_cookies, 0}));
    return res;
}

crow::response post_logout(const RequestContext& ctx)
{
    if (auto token = get_cookie(ctx.req, ctx.svc.session_cookie_name())) ctx.svc.sessions.destroy(*token);
    seclog::event("logout", {{"user_id", uid(ctx)}});
    auto res = redirect("/login");
    res.add_header("Set-Cookie", make_cookie(ctx.svc.session_cookie_name(), "", {ctx.svc.cfg.secure_cookies, 0}));
    return res;
}

// ----- Dashboard / history / receipts --------------------------------------

crow::response get_dashboard(const RequestContext& ctx)
{
    auto c = base_context(ctx, "Home");
    c["balance"] = money::format_cents(ctx.svc.history.balance(ctx.session->user_id));
    c["phone"] = ctx.session->phone;
    add_transactions(c, ctx.svc.history.list_for(ctx.session->user_id, kDashboardItems));
    return render(ctx, 200, "dashboard.html", c);
}

crow::response get_history(const RequestContext& ctx)
{
    auto c = base_context(ctx, "History");
    add_transactions(c, ctx.svc.history.list_for(ctx.session->user_id, kHistoryItems));
    return render(ctx, 200, "history.html", c);
}

crow::response get_receipt(const RequestContext& ctx, std::int64_t id)
{
    const auto receipt = ctx.svc.history.receipt_for(id, ctx.session->user_id);
    if (!receipt) {
        // Same response whether the receipt is someone else's or does not exist.
        seclog::event("access_denied",
                      {{"user_id", uid(ctx)}, {"resource", "receipt"}, {"receipt_id", std::to_string(id)}, {"ip", ctx.ip}});
        return error_page(ctx, 404, "Receipt not found", "That receipt does not exist.");
    }
    auto c = base_context(ctx, "Receipt");
    c["receipt_id"] = receipt->id;
    c["outgoing"] = receipt->outgoing;
    c["counterparty"] = receipt->counterparty;
    c["amount"] = money::format_cents(receipt->amount_cents);
    c["kind"] = receipt->kind == "qr" ? "QR payment" : "Phone transfer";
    c["created_at"] = receipt->created_at;
    c["reference"] = receipt->authorization_id;
    return render(ctx, 200, "receipt.html", c);
}

// ----- QR payments ----------------------------------------------------------

struct MerchantCheck {
    std::optional<payments::Merchant> merchant;
    std::optional<crow::response> error;
};

MerchantCheck check_qr(const RequestContext& ctx, const std::string& payload)
{
    const auto v = ctx.svc.signer.verify(payload);
    if (!v.ok()) {
        seclog::event("qr_rejected", {{"reason", qr::to_string(v.error)}, {"user_id", uid(ctx)}, {"ip", ctx.ip}});
        return {std::nullopt, error_page(ctx, 400, "Invalid QR code", kBadQrMessage)};
    }
    auto merchant = ctx.svc.authorizations.find_merchant(v.merchant_id);
    if (!merchant) {
        seclog::event("qr_rejected", {{"reason", "unknown_merchant"}, {"user_id", uid(ctx)}, {"ip", ctx.ip}});
        return {std::nullopt, error_page(ctx, 400, "Invalid QR code", kBadQrMessage)};
    }
    if (merchant->user_id == ctx.session->user_id)
        return {std::nullopt, error_page(ctx, 400, "Cannot pay yourself", "This is your own merchant QR code.")};
    return {std::move(merchant), std::nullopt};
}

crow::response review_page(const RequestContext& ctx, int code, const payments::Merchant& m, const std::string& qr,
                           const std::string& error)
{
    auto c = base_context(ctx, "Review payment");
    c["merchant_name"] = m.display_name;
    c["qr"] = qr;
    c["amount"] = ctx.field("amount");
    if (!error.empty()) c["error"] = error;
    return render(ctx, code, "review.html", c);
}

crow::response get_scan(const RequestContext& ctx) { return render(ctx, 200, "scan.html", base_context(ctx, "Scan to pay")); }

crow::response post_pay_review(const RequestContext& ctx)
{
    const std::string qr = ctx.field("qr");
    auto check = check_qr(ctx, qr);
    if (check.error) return std::move(*check.error);
    return review_page(ctx, 200, *check.merchant, qr, "");
}

crow::response post_pay_authorize(const RequestContext& ctx)
{
    // The QR is verified again: the hidden field is client-controlled.
    const std::string qr = ctx.field("qr");
    auto check = check_qr(ctx, qr);
    if (check.error) return std::move(*check.error);

    const auto amount = money::parse_amount(ctx.field("amount"));
    if (!amount) return review_page(ctx, 400, *check.merchant, qr, kAmountError);

    const auto id = ctx.svc.authorizations.create(ctx.session->user_id, check.merchant->user_id, *amount,
                                                  payments::Kind::Qr, ctx.session->token_hash);
    seclog::event("payment_authorized", {{"user_id", uid(ctx)}, {"kind", "qr"}, {"auth", id}});
    return redirect("/authorizations/" + id);
}

// ----- Phone transfers ------------------------------------------------------

crow::response transfer_page(const RequestContext& ctx, int code, const std::string& error)
{
    auto c = base_context(ctx, "Send money");
    c["phone"] = ctx.field("phone");
    c["amount"] = ctx.field("amount");
    if (!error.empty()) c["error"] = error;
    return render(ctx, code, "transfer.html", c);
}

crow::response get_transfer(const RequestContext& ctx) { return transfer_page(ctx, 200, ""); }

crow::response post_transfer_authorize(const RequestContext& ctx)
{
    if (!ctx.svc.lookup_limiter.allow("user:" + uid(ctx)) || !ctx.svc.lookup_limiter.allow("ip:" + ctx.ip)) {
        seclog::event("lookup_rate_limited", {{"user_id", uid(ctx)}, {"ip", ctx.ip}});
        return transfer_page(ctx, 429, "Too many lookups. Wait a minute and try again.");
    }
    const auto phone = validate::phone(ctx.field("phone"));
    if (!phone) return transfer_page(ctx, 400, "Enter a US phone number in +1 format, for example +1 555 555 0101.");
    const auto amount = money::parse_amount(ctx.field("amount"));
    if (!amount) return transfer_page(ctx, 400, kAmountError);

    const auto recipient = ctx.svc.authorizations.find_recipient(*phone);
    if (!recipient) return transfer_page(ctx, 404, "No ScanPay account uses that phone number.");
    if (recipient->user_id == ctx.session->user_id) return transfer_page(ctx, 400, "You cannot send money to yourself.");

    const auto id = ctx.svc.authorizations.create(ctx.session->user_id, recipient->user_id, *amount,
                                                  payments::Kind::Phone, ctx.session->token_hash);
    seclog::event("payment_authorized", {{"user_id", uid(ctx)}, {"kind", "phone"}, {"auth", id}});
    return redirect("/authorizations/" + id);
}

// ----- Authorization confirm ------------------------------------------------

crow::response expired_page(const RequestContext& ctx)
{
    return error_page(ctx, 410, "Authorization expired",
                      "Payment authorizations are valid for 2 minutes. Nothing was charged. Please start again.");
}

crow::response get_authorization(const RequestContext& ctx, const std::string& id)
{
    const auto a = ctx.svc.authorizations.load_for(id, ctx.session->user_id, ctx.session->token_hash);
    if (!a) return error_page(ctx, 404, "Not found", "That payment authorization does not exist.");
    if (a->status == "used") return redirect("/receipts/" + std::to_string(a->transaction_id));
    if (a->status == "expired" || a->expired) return expired_page(ctx);

    auto c = base_context(ctx, "Confirm payment");
    c["authorization_id"] = a->id;
    c["payee"] = a->payee_display;
    c["amount"] = money::format_cents(a->amount_cents);
    c["is_qr"] = a->kind == "qr";
    c["seconds_left"] = a->seconds_left;
    return render(ctx, 200, "confirm.html", c);
}

crow::response post_pay_confirm(const RequestContext& ctx)
{
    const auto r =
        ctx.svc.transfers.confirm(ctx.field("authorization_id"), ctx.session->user_id, ctx.session->token_hash);
    switch (r.status) {
        case payments::ConfirmStatus::Completed:
        case payments::ConfirmStatus::AlreadyCompleted:
            return redirect("/receipts/" + std::to_string(r.transaction_id));
        case payments::ConfirmStatus::Expired:
            return expired_page(ctx);
        case payments::ConfirmStatus::InsufficientFunds:
            return error_page(ctx, 409, "Insufficient balance",
                              "Your balance is too low for this payment. Nothing was charged.");
        case payments::ConfirmStatus::NotFound:
        case payments::ConfirmStatus::SessionMismatch:
            break;
    }
    return error_page(ctx, 404, "Not found", "That payment authorization does not exist.");
}

// ----- Merchant -------------------------------------------------------------

crow::response get_merchant_qr(const RequestContext& ctx)
{
    const auto m = ctx.svc.authorizations.find_merchant_by_user(ctx.session->user_id);
    if (!m) return error_page(ctx, 404, "Not found", "No merchant profile for this account.");
    auto c = base_context(ctx, "Your QR code");
    c["merchant_name"] = m->display_name;
    c["payload"] = ctx.svc.signer.payload_for(m->merchant_id);
    return render(ctx, 200, "merchant_qr.html", c);
}

crow::response get_merchant_qr_svg(const RequestContext& ctx)
{
    const auto m = ctx.svc.authorizations.find_merchant_by_user(ctx.session->user_id);
    if (!m) return error_page(ctx, 404, "Not found", "No merchant profile for this account.");
    crow::response res(200, qr::render_svg(ctx.svc.signer.payload_for(m->merchant_id)));
    res.set_header("Content-Type", "image/svg+xml");
    return res;
}

} // namespace

void register_routes(App& app, Services& svc)
{
    auto wrap = [&svc](Access access, crow::response (*fn)(const RequestContext&)) {
        return [&svc, access, fn](const crow::request& req) { return guarded(svc, req, access, fn); };
    };

    CROW_ROUTE(app, "/healthz")([] { return crow::response(200, "ok"); });

    CROW_ROUTE(app, "/login").methods(HTTPMethod::Get)(wrap(Access::Public, get_login));
    CROW_ROUTE(app, "/login").methods(HTTPMethod::Post)(wrap(Access::Public, post_login));
    CROW_ROUTE(app, "/logout").methods(HTTPMethod::Post)(wrap(Access::User, post_logout));

    CROW_ROUTE(app, "/").methods(HTTPMethod::Get)(wrap(Access::User, get_dashboard));
    CROW_ROUTE(app, "/history").methods(HTTPMethod::Get)(wrap(Access::User, get_history));
    CROW_ROUTE(app, "/receipts/<int>").methods(HTTPMethod::Get)([&svc](const crow::request& req, std::int64_t id) {
        return guarded(svc, req, Access::User, [id](const RequestContext& ctx) { return get_receipt(ctx, id); });
    });

    CROW_ROUTE(app, "/scan").methods(HTTPMethod::Get)(wrap(Access::User, get_scan));
    CROW_ROUTE(app, "/pay/review").methods(HTTPMethod::Post)(wrap(Access::User, post_pay_review));
    CROW_ROUTE(app, "/pay/authorize").methods(HTTPMethod::Post)(wrap(Access::User, post_pay_authorize));
    CROW_ROUTE(app, "/pay/confirm").methods(HTTPMethod::Post)(wrap(Access::User, post_pay_confirm));

    CROW_ROUTE(app, "/transfer").methods(HTTPMethod::Get)(wrap(Access::User, get_transfer));
    CROW_ROUTE(app, "/transfer/authorize").methods(HTTPMethod::Post)(wrap(Access::User, post_transfer_authorize));

    CROW_ROUTE(app, "/authorizations/<string>")
        .methods(HTTPMethod::Get)([&svc](const crow::request& req, const std::string& id) {
            return guarded(svc, req, Access::User, [&id](const RequestContext& ctx) { return get_authorization(ctx, id); });
        });

    CROW_ROUTE(app, "/merchant/qr").methods(HTTPMethod::Get)(wrap(Access::Merchant, get_merchant_qr));
    CROW_ROUTE(app, "/merchant/qr.svg").methods(HTTPMethod::Get)(wrap(Access::Merchant, get_merchant_qr_svg));

    CROW_CATCHALL_ROUTE(app)([&svc](const crow::request& req) {
        return guarded(svc, req, Access::Public, [](const RequestContext& ctx) {
            return error_page(ctx, 404, "Not found", "That page does not exist.");
        });
    });
}

} // namespace web
