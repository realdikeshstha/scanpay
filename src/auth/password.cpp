#include "auth/password.h"

#include <stdexcept>

#include <sodium.h>

namespace auth {

std::string hash_password(std::string_view password)
{
    char out[crypto_pwhash_STRBYTES];
    if (crypto_pwhash_argon2id_str(out, password.data(), password.size(),
                                   crypto_pwhash_argon2id_OPSLIMIT_INTERACTIVE,
                                   crypto_pwhash_argon2id_MEMLIMIT_INTERACTIVE) != 0) {
        throw std::runtime_error("password hashing failed (out of memory)");
    }
    return std::string(out);
}

bool verify_password(const std::string& hash, std::string_view password)
{
    return crypto_pwhash_argon2id_str_verify(hash.c_str(), password.data(), password.size()) == 0;
}

void burn_verification(std::string_view password)
{
    static const std::string dummy = hash_password("scanpay-timing-equalizer");
    (void)verify_password(dummy, password);
}

} // namespace auth
