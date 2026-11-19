#include "validate.h"

namespace validate {
namespace {

bool is_digit(char c) { return c >= '0' && c <= '9'; }

bool is_hex(char c) { return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

// Length in bytes of the UTF-8 sequence starting with `lead` (1 for invalid).
std::size_t utf8_len(unsigned char lead)
{
    if (lead >= 0xF0 && lead <= 0xF4) return 4;
    if (lead >= 0xE0) return 3;
    if (lead >= 0xC2) return 2;
    return 1;
}

} // namespace

std::optional<std::string> phone(std::string_view raw)
{
    if (raw.size() > kMaxPhoneInput) return std::nullopt;
    std::string out;
    out.reserve(raw.size());
    for (char c : raw) {
        if (c == ' ' || c == '-' || c == '.' || c == '(' || c == ')') continue;
        out += c;
    }
    if (out.size() != 12 || out[0] != '+' || out[1] != '1') return std::nullopt;
    for (std::size_t i = 2; i < out.size(); ++i)
        if (!is_digit(out[i])) return std::nullopt;
    return out;
}

bool is_uuid(std::string_view s)
{
    if (s.size() != 36) return false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const bool dash_pos = (i == 8 || i == 13 || i == 18 || i == 23);
        if (dash_pos ? s[i] != '-' : !is_hex(s[i])) return false;
    }
    return true;
}

std::string mask_name(std::string_view full_name)
{
    auto first_end = full_name.find(' ');
    if (first_end == std::string_view::npos) return std::string(full_name);
    auto last_start = full_name.find_last_of(' ') + 1;
    if (last_start >= full_name.size()) return std::string(full_name.substr(0, first_end));
    auto initial_len = utf8_len(static_cast<unsigned char>(full_name[last_start]));
    if (last_start + initial_len > full_name.size()) initial_len = 1;
    std::string out(full_name.substr(0, first_end));
    out += ' ';
    out += full_name.substr(last_start, initial_len);
    out += '.';
    return out;
}

std::string mask_phone(std::string_view number)
{
    if (number.size() < 4) return "***";
    return "***-***-" + std::string(number.substr(number.size() - 4));
}

} // namespace validate
