#include "web/cookies.h"

namespace web {

std::optional<std::string> get_cookie(const crow::request& req, std::string_view name)
{
    const std::string& header = req.get_header_value("Cookie");
    std::string_view rest(header);
    while (!rest.empty()) {
        const auto sep = rest.find(';');
        std::string_view pair = rest.substr(0, sep);
        rest = sep == std::string_view::npos ? std::string_view{} : rest.substr(sep + 1);
        while (!pair.empty() && pair.front() == ' ') pair.remove_prefix(1);
        const auto eq = pair.find('=');
        if (eq == std::string_view::npos) continue;
        if (pair.substr(0, eq) == name) return std::string(pair.substr(eq + 1));
    }
    return std::nullopt;
}

std::string make_cookie(std::string_view name, std::string_view value, const CookieOptions& opts)
{
    std::string c;
    c += name;
    c += '=';
    c += value;
    c += "; Path=/; HttpOnly; SameSite=Strict";
    if (opts.secure) c += "; Secure";
    if (opts.max_age >= 0) c += "; Max-Age=" + std::to_string(opts.max_age);
    return c;
}

} // namespace web
