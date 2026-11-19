#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "config.h"

namespace qr {

enum class QrError {
    None,
    TooLong,
    LooksLikeUrl,
    Malformed,
    BadSignature,
};

const char* to_string(QrError e);

struct QrVerification {
    QrError error = QrError::Malformed;
    std::int64_t merchant_id = 0;

    bool ok() const { return error == QrError::None; }
};

// Reusable merchant QR payload:  scanpay:v1:<merchant_id>:<tag>
// where tag = base64url(HMAC-SHA512-256(key, "scanpay:v1:<merchant_id>")).
// The key lives only in the server environment, so a printed code cannot be
// edited to point at a different merchant without the server noticing.
class Signer {
public:
    explicit Signer(const config::QrKey& key) : key_(key) {}
    ~Signer();
    Signer(const Signer&) = delete;
    Signer& operator=(const Signer&) = delete;

    std::string payload_for(std::int64_t merchant_id) const;

    // Strict parser: anything other than the exact format above is rejected.
    QrVerification verify(std::string_view payload) const;

private:
    config::QrKey key_;
};

} // namespace qr
