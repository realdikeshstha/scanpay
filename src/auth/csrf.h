#pragma once

#include <string_view>

namespace auth {

inline constexpr std::string_view kCsrfField = "csrf_token";

// Synchronizer-token check: the token submitted with a form must equal the one
// bound to the session. Compared in constant time; empty never matches.
bool csrf_matches(std::string_view expected, std::string_view submitted);

} // namespace auth
