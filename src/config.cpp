#include "config.h"

#include <charconv>
#include <cstdlib>

#include <sodium.h>

namespace config {
namespace {

std::string env_or(const char* name, std::string fallback)
{
    const char* value = std::getenv(name);
    return (value && *value) ? std::string(value) : std::move(fallback);
}

std::string required_env(const char* name)
{
    std::string value = env_or(name, "");
    if (value.empty()) throw ConfigError(std::string("missing required environment variable ") + name);
    return value;
}

template<typename Int>
Int parse_int(const std::string& text, const char* name, Int min, Int max)
{
    Int value{};
    auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || ptr != text.data() + text.size() || value < min || value > max)
        throw ConfigError(std::string("invalid value for ") + name);
    return value;
}

} // namespace

QrKey parse_qr_key(const std::string& hex)
{
    QrKey key{};
    std::size_t len = 0;
    const char* end = nullptr;
    if (hex.size() != kQrKeyBytes * 2
        || sodium_hex2bin(key.data(), key.size(), hex.data(), hex.size(), nullptr, &len, &end) != 0
        || len != kQrKeyBytes || end != hex.data() + hex.size()) {
        sodium_memzero(key.data(), key.size());
        throw ConfigError("SCANPAY_QR_KEY must be 64 hex characters (generate with: openssl rand -hex 32)");
    }
    return key;
}

Config Config::from_env()
{
    Config cfg;
    cfg.database_url = required_env("DATABASE_URL");
    cfg.qr_key = parse_qr_key(required_env("SCANPAY_QR_KEY"));
    cfg.tls_cert_path = env_or("SCANPAY_TLS_CERT", "");
    cfg.tls_key_path = env_or("SCANPAY_TLS_KEY", "");
    if (cfg.tls_cert_path.empty() != cfg.tls_key_path.empty())
        throw ConfigError("set both SCANPAY_TLS_CERT and SCANPAY_TLS_KEY, or neither");
    cfg.bind_address = env_or("SCANPAY_BIND", cfg.bind_address);
    cfg.port = parse_int<std::uint16_t>(env_or("SCANPAY_PORT", "8443"), "SCANPAY_PORT", 1, 65535);
    cfg.template_dir = env_or("SCANPAY_TEMPLATES", cfg.template_dir);
    cfg.security_log_path = env_or("SCANPAY_SECURITY_LOG", "");
    cfg.db_pool_size = parse_int<std::size_t>(env_or("SCANPAY_DB_POOL", "8"), "SCANPAY_DB_POOL", 1, 64);
    cfg.secure_cookies = env_or("SCANPAY_SECURE_COOKIES", "1") != "0";
    return cfg;
}

} // namespace config
