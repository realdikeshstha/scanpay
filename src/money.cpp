#include "money.h"

#include <charconv>

namespace money {
namespace {

constexpr std::size_t kMaxInputLength = 10;  // "500.00" plus generous slack
constexpr std::size_t kMaxDollarDigits = 6;

bool all_digits(std::string_view s)
{
    if (s.empty()) return false;
    for (char c : s)
        if (c < '0' || c > '9') return false;
    return true;
}

} // namespace

std::optional<std::int64_t> parse_amount(std::string_view text)
{
    while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
    while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
    if (text.empty() || text.size() > kMaxInputLength) return std::nullopt;

    std::string_view dollars = text;
    std::string_view fraction;
    if (auto dot = text.find('.'); dot != std::string_view::npos) {
        dollars = text.substr(0, dot);
        fraction = text.substr(dot + 1);
        if (fraction.empty() || fraction.size() > 2 || !all_digits(fraction)) return std::nullopt;
    }
    if (!all_digits(dollars) || dollars.size() > kMaxDollarDigits) return std::nullopt;

    std::int64_t whole = 0;
    auto [ptr, ec] = std::from_chars(dollars.data(), dollars.data() + dollars.size(), whole);
    if (ec != std::errc{} || ptr != dollars.data() + dollars.size()) return std::nullopt;

    std::int64_t cents = 0;
    if (!fraction.empty()) {
        cents = (fraction[0] - '0') * 10;
        if (fraction.size() == 2) cents += fraction[1] - '0';
    }

    // whole has at most 6 digits, so this cannot overflow int64.
    const std::int64_t total = whole * 100 + cents;
    if (total < kMinPaymentCents || total > kMaxPaymentCents) return std::nullopt;
    return total;
}

std::string format_cents(std::int64_t cents)
{
    const bool negative = cents < 0;
    // Work in unsigned so INT64_MIN does not overflow on negation.
    std::uint64_t magnitude = negative ? (~static_cast<std::uint64_t>(cents) + 1) : static_cast<std::uint64_t>(cents);
    std::string out = negative ? "-$" : "$";
    out += std::to_string(magnitude / 100);
    out += '.';
    const auto rem = magnitude % 100;
    out += static_cast<char>('0' + rem / 10);
    out += static_cast<char>('0' + rem % 10);
    return out;
}

} // namespace money
