#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace crypto {

// Must be called once before any other libsodium use. Throws on failure.
void init();

// `bytes` of CSPRNG output, base64url-encoded without padding.
std::string random_token(std::size_t bytes = 32);

std::string sha256_hex(std::string_view data);

std::string base64url_encode(const unsigned char* data, std::size_t len);
std::optional<std::vector<unsigned char>> base64url_decode(std::string_view text);

// Constant-time comparison; returns false for different lengths.
bool constant_time_equal(std::string_view a, std::string_view b);

} // namespace crypto
