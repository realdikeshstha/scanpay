#pragma once

#include <string>
#include <string_view>

namespace auth {

// Argon2id (libsodium crypto_pwhash_str) with a random salt per hash. The
// returned string encodes the algorithm, parameters and salt.
std::string hash_password(std::string_view password);

bool verify_password(const std::string& hash, std::string_view password);

// Runs a verification against a fixed dummy hash so that "unknown account" and
// "wrong password" take the same time.
void burn_verification(std::string_view password);

} // namespace auth
