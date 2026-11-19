#include <doctest/doctest.h>

#include "auth/csrf.h"
#include "auth/password.h"
#include "config.h"
#include "crypto.h"

TEST_CASE("passwords are hashed with salted Argon2id")
{
    const auto h1 = auth::hash_password("correct horse");
    const auto h2 = auth::hash_password("correct horse");
    CHECK(h1.rfind("$argon2id$", 0) == 0);
    CHECK(h1 != h2);  // random salt per hash
    CHECK(h1.find("correct horse") == std::string::npos);
    CHECK(auth::verify_password(h1, "correct horse"));
    CHECK_FALSE(auth::verify_password(h1, "correct horsE"));
    CHECK_FALSE(auth::verify_password(h1, ""));
}

TEST_CASE("CSRF tokens must match exactly")
{
    const auto token = crypto::random_token();
    CHECK(auth::csrf_matches(token, token));
    CHECK_FALSE(auth::csrf_matches(token, ""));
    CHECK_FALSE(auth::csrf_matches("", ""));
    CHECK_FALSE(auth::csrf_matches(token, token.substr(1)));
    CHECK_FALSE(auth::csrf_matches(token, crypto::random_token()));
}

TEST_CASE("random tokens are 256-bit base64url")
{
    const auto a = crypto::random_token();
    CHECK(a.size() == 43);
    CHECK(a != crypto::random_token());
    CHECK(a.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_") == std::string::npos);
}

TEST_CASE("sha256 hex")
{
    CHECK(crypto::sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("QR key must be 64 hex characters")
{
    CHECK_NOTHROW(config::parse_qr_key(std::string(64, 'a')));
    CHECK_THROWS_AS(config::parse_qr_key("replace-me"), config::ConfigError);
    CHECK_THROWS_AS(config::parse_qr_key(std::string(63, 'a')), config::ConfigError);
    CHECK_THROWS_AS(config::parse_qr_key(std::string(64, 'g')), config::ConfigError);
}
