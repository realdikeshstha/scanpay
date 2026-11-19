#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace validate {

inline constexpr std::size_t kMaxPhoneInput = 20;
inline constexpr std::size_t kMaxPasswordBytes = 128;
inline constexpr std::size_t kMaxQrPayload = 128;

// Accepts "+1" followed by exactly ten digits. Spaces, dashes, dots and
// parentheses are stripped first so "+1 (555) 555-0101" is accepted.
std::optional<std::string> phone(std::string_view raw);

bool is_uuid(std::string_view s);

// "Alice Carter" -> "Alice C."
std::string mask_name(std::string_view full_name);

// "+15555550101" -> "***-***-0101"
std::string mask_phone(std::string_view number);

} // namespace validate
