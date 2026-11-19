#include <doctest/doctest.h>

#include "qr/qr_svg.h"
#include "qr/qr_token.h"

namespace {

config::QrKey key_of(unsigned char fill)
{
    config::QrKey k{};
    k.fill(fill);
    return k;
}

} // namespace

TEST_CASE("a signed merchant QR verifies")
{
    qr::Signer signer(key_of(7));
    const auto payload = signer.payload_for(42);
    CHECK(payload.rfind("scanpay:v1:42:", 0) == 0);
    const auto v = signer.verify(payload);
    REQUIRE(v.ok());
    CHECK(v.merchant_id == 42);
}

TEST_CASE("changing the merchant id invalidates the tag")
{
    qr::Signer signer(key_of(7));
    std::string payload = signer.payload_for(1);
    payload.replace(payload.find(":1:"), 3, ":2:");
    CHECK(signer.verify(payload).error == qr::QrError::BadSignature);
}

TEST_CASE("changing one character of the tag invalidates it")
{
    qr::Signer signer(key_of(7));
    std::string payload = signer.payload_for(1);
    payload.back() = payload.back() == 'A' ? 'B' : 'A';
    CHECK(signer.verify(payload).error == qr::QrError::BadSignature);
}

TEST_CASE("a QR signed with another key is rejected")
{
    qr::Signer real(key_of(7));
    qr::Signer forger(key_of(8));
    CHECK(real.verify(forger.payload_for(1)).error == qr::QrError::BadSignature);
}

TEST_CASE("URL and malformed payloads are rejected")
{
    qr::Signer signer(key_of(7));
    CHECK(signer.verify("https://evil.example/pay?to=attacker").error == qr::QrError::LooksLikeUrl);
    CHECK(signer.verify("HTTP://EVIL.EXAMPLE").error == qr::QrError::LooksLikeUrl);
    CHECK(signer.verify("javascript:alert(1)").error == qr::QrError::LooksLikeUrl);
    CHECK(signer.verify("scanpay:v1:1:https://x").error == qr::QrError::LooksLikeUrl);
    CHECK(signer.verify(std::string(500, 'a')).error == qr::QrError::TooLong);

    const std::string tag = signer.payload_for(1).substr(std::string("scanpay:v1:1:").size());
    for (const std::string& bad : {std::string(""), std::string("scanpay:v1:"), std::string("scanpay:v2:1:") + tag,
                                   std::string("scanpay:v1:01:") + tag, std::string("scanpay:v1:-1:") + tag,
                                   std::string("scanpay:v1:abc:") + tag, std::string("scanpay:v1:1:") + tag + "x",
                                   std::string("scanpay:v1:1:") + tag.substr(1), std::string("scanpay:v1:1:") + tag + ":",
                                   std::string("scanpay:v1:1:<script>")}) {
        INFO("payload: ", bad);
        CHECK(signer.verify(bad).error == qr::QrError::Malformed);
    }
}

TEST_CASE("QR SVG renders")
{
    const auto svg = qr::render_svg("scanpay:v1:1:abc");
    CHECK(svg.find("<svg") != std::string::npos);
    CHECK(svg.find("</svg>") != std::string::npos);
}
