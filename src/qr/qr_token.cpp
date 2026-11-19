#include "qr/qr_token.h"

#include <algorithm>
#include <cctype>
#include <charconv>

#include <sodium.h>

#include "crypto.h"
#include "validate.h"

namespace qr {
namespace {

constexpr std::string_view kPrefix = "scanpay:v1:";
constexpr std::size_t kMaxIdDigits = 12;
constexpr std::size_t kTagChars = 43;  // base64url of 32 bytes, unpadded

bool is_b64url(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
}

bool looks_like_url(std::string_view s)
{
    std::string lower(s);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower.find("://") != std::string::npos || lower.rfind("http", 0) == 0 || lower.rfind("www.", 0) == 0
           || lower.rfind("javascript:", 0) == 0 || lower.rfind("data:", 0) == 0;
}

} // namespace

const char* to_string(QrError e)
{
    switch (e) {
        case QrError::None: return "none";
        case QrError::TooLong: return "too_long";
        case QrError::LooksLikeUrl: return "url_payload";
        case QrError::Malformed: return "malformed";
        case QrError::BadSignature: return "bad_signature";
    }
    return "unknown";
}

Signer::~Signer() { sodium_memzero(key_.data(), key_.size()); }

std::string Signer::payload_for(std::int64_t merchant_id) const
{
    const std::string message = std::string(kPrefix) + std::to_string(merchant_id);
    unsigned char tag[crypto_auth_BYTES];
    crypto_auth(tag, reinterpret_cast<const unsigned char*>(message.data()), message.size(), key_.data());
    return message + ":" + crypto::base64url_encode(tag, sizeof tag);
}

QrVerification Signer::verify(std::string_view payload) const
{
    QrVerification out;
    if (payload.size() > validate::kMaxQrPayload) {
        out.error = QrError::TooLong;
        return out;
    }
    if (looks_like_url(payload)) {
        out.error = QrError::LooksLikeUrl;
        return out;
    }
    if (payload.substr(0, kPrefix.size()) != kPrefix) return out;

    const std::string_view rest = payload.substr(kPrefix.size());
    const auto colon = rest.find(':');
    if (colon == std::string_view::npos) return out;
    const std::string_view id_text = rest.substr(0, colon);
    const std::string_view tag_text = rest.substr(colon + 1);

    if (id_text.empty() || id_text.size() > kMaxIdDigits || id_text[0] == '0') return out;
    if (!std::all_of(id_text.begin(), id_text.end(), [](char c) { return c >= '0' && c <= '9'; })) return out;
    if (tag_text.size() != kTagChars || !std::all_of(tag_text.begin(), tag_text.end(), is_b64url)) return out;

    std::int64_t id = 0;
    auto [ptr, ec] = std::from_chars(id_text.data(), id_text.data() + id_text.size(), id);
    if (ec != std::errc{} || ptr != id_text.data() + id_text.size()) return out;

    const auto tag = crypto::base64url_decode(tag_text);
    if (!tag || tag->size() != crypto_auth_BYTES) return out;

    const std::string_view signed_part = payload.substr(0, kPrefix.size() + id_text.size());
    if (crypto_auth_verify(tag->data(), reinterpret_cast<const unsigned char*>(signed_part.data()),
                           signed_part.size(), key_.data()) != 0) {
        out.error = QrError::BadSignature;
        return out;
    }
    out.error = QrError::None;
    out.merchant_id = id;
    return out;
}

} // namespace qr
