#include "crypto.h"

#include <stdexcept>

#include <sodium.h>

namespace crypto {

void init()
{
    if (sodium_init() < 0) throw std::runtime_error("libsodium failed to initialize");
}

std::string base64url_encode(const unsigned char* data, std::size_t len)
{
    constexpr int variant = sodium_base64_VARIANT_URLSAFE_NO_PADDING;
    std::string out(sodium_base64_encoded_len(len, variant), '\0');
    sodium_bin2base64(out.data(), out.size(), data, len, variant);
    out.resize(out.find('\0'));
    return out;
}

std::optional<std::vector<unsigned char>> base64url_decode(std::string_view text)
{
    std::vector<unsigned char> out(text.size());
    std::size_t out_len = 0;
    const char* end = nullptr;
    if (sodium_base642bin(out.data(), out.size(), text.data(), text.size(), nullptr, &out_len, &end,
                          sodium_base64_VARIANT_URLSAFE_NO_PADDING) != 0
        || end != text.data() + text.size()) {
        return std::nullopt;
    }
    out.resize(out_len);
    return out;
}

std::string random_token(std::size_t bytes)
{
    std::vector<unsigned char> buf(bytes);
    randombytes_buf(buf.data(), buf.size());
    std::string token = base64url_encode(buf.data(), buf.size());
    sodium_memzero(buf.data(), buf.size());
    return token;
}

std::string sha256_hex(std::string_view data)
{
    unsigned char digest[crypto_hash_sha256_BYTES];
    crypto_hash_sha256(digest, reinterpret_cast<const unsigned char*>(data.data()), data.size());
    char hex[crypto_hash_sha256_BYTES * 2 + 1];
    sodium_bin2hex(hex, sizeof hex, digest, sizeof digest);
    return std::string(hex, crypto_hash_sha256_BYTES * 2);
}

bool constant_time_equal(std::string_view a, std::string_view b)
{
    if (a.size() != b.size()) return false;
    if (a.empty()) return true;
    return sodium_memcmp(a.data(), b.data(), a.size()) == 0;
}

} // namespace crypto
