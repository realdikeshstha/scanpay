#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "crow.h"

namespace web {

struct CookieOptions {
    bool secure = true;
    int max_age = -1;  // -1 = session cookie, 0 = delete
};

std::optional<std::string> get_cookie(const crow::request& req, std::string_view name);

// Always HttpOnly, SameSite=Strict, Path=/.
std::string make_cookie(std::string_view name, std::string_view value, const CookieOptions& opts);

} // namespace web
