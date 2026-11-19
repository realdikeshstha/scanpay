#include <doctest/doctest.h>

#include "money.h"

using money::parse_amount;

TEST_CASE("valid amounts are parsed into integer cents")
{
    CHECK(parse_amount("0.01") == 1);
    CHECK(parse_amount("1") == 100);
    CHECK(parse_amount("4.5") == 450);
    CHECK(parse_amount("4.75") == 475);
    CHECK(parse_amount("500") == 50'000);
    CHECK(parse_amount("500.00") == 50'000);
    CHECK(parse_amount(" 12.34 ") == 1234);
    CHECK(parse_amount("007.10") == 710);
}

TEST_CASE("amounts outside $0.01 - $500.00 are rejected")
{
    CHECK_FALSE(parse_amount("0"));
    CHECK_FALSE(parse_amount("0.00"));
    CHECK_FALSE(parse_amount("500.01"));
    CHECK_FALSE(parse_amount("501"));
    CHECK_FALSE(parse_amount("999999"));
}

TEST_CASE("malformed amounts are rejected")
{
    for (const char* bad : {"", " ", "-5", "+5", "1.234", "1.", ".5", "1e3", "1,000", "$5", "5 .00", "abc",
                            "1.2.3", "0x10", "NaN", "inf", "12345678901", "99999999999999999999999"}) {
        INFO("input: ", bad);
        CHECK_FALSE(parse_amount(bad));
    }
}

TEST_CASE("cents format as dollars")
{
    CHECK(money::format_cents(0) == "$0.00");
    CHECK(money::format_cents(5) == "$0.05");
    CHECK(money::format_cents(475) == "$4.75");
    CHECK(money::format_cents(50'000) == "$500.00");
    CHECK(money::format_cents(-125) == "-$1.25");
    CHECK(money::format_cents(INT64_MIN) == "-$92233720368547758.08");
}
