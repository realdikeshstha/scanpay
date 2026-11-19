#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace config {

inline constexpr std::size_t kQrKeyBytes = 32;
using QrKey = std::array<unsigned char, kQrKeyBytes>;

struct ConfigError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// All secrets come from the environment (or a Docker env file), never from
// source code. See .env.example.
struct Config {
    std::string database_url;
    QrKey qr_key{};
    std::string tls_cert_path;  // empty = plain HTTP (development only)
    std::string tls_key_path;
    std::string bind_address = "0.0.0.0";
    std::uint16_t port = 8443;
    std::string template_dir = "templates";
    std::string security_log_path;  // empty = stderr
    std::size_t db_pool_size = 8;
    bool secure_cookies = true;

    static Config from_env();
};

// Parses exactly 64 hex characters into a key. Throws ConfigError otherwise.
QrKey parse_qr_key(const std::string& hex);

} // namespace config
