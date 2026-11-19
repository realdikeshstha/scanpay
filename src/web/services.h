#pragma once

#include <memory>

#include "auth/login.h"
#include "auth/rate_limit.h"
#include "auth/session.h"
#include "config.h"
#include "db/pool.h"
#include "payments/authorization.h"
#include "payments/history.h"
#include "payments/transfer.h"
#include "qr/qr_token.h"
#include "web/views.h"

namespace web {

// Everything a request handler needs, wired together once at startup.
struct Services {
    explicit Services(const config::Config& cfg);

    config::Config cfg;
    db::Pool pool;
    auth::SessionStore sessions;
    auth::RateLimiter login_limiter;   // per IP
    auth::RateLimiter lookup_limiter;  // per user and per IP, phone lookups
    auth::LoginService login;
    qr::Signer signer;
    payments::AuthorizationService authorizations;
    payments::TransferService transfers;
    payments::HistoryService history;
    Views views;

    std::string session_cookie_name() const { return cfg.secure_cookies ? "__Host-sp_session" : "sp_session"; }
    std::string login_csrf_cookie_name() const { return cfg.secure_cookies ? "__Host-sp_login" : "sp_login"; }
};

} // namespace web
