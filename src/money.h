#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace money {

inline constexpr std::int64_t kMinPaymentCents = 1;       // $0.01
inline constexpr std::int64_t kMaxPaymentCents = 50'000;  // $500.00

// Parses a user-entered dollar amount ("12", "12.3", "12.34") into cents.
// Rejects signs, exponents, separators, more than two decimals and anything
// outside [$0.01, $500.00]. No floating point is involved.
std::optional<std::int64_t> parse_amount(std::string_view text);

// 1234 -> "$12.34"
std::string format_cents(std::int64_t cents);

} // namespace money
