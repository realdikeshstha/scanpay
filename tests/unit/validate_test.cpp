#include <doctest/doctest.h>

#include "validate.h"

TEST_CASE("phone numbers must be +1 followed by ten digits")
{
    CHECK(validate::phone("+15555550101") == "+15555550101");
    CHECK(validate::phone("+1 (555) 555-0101") == "+15555550101");
    CHECK(validate::phone("+1.555.555.0101") == "+15555550101");

    for (const char* bad : {"", "5555550101", "15555550101", "+25555550101", "+1555555010", "+155555501011",
                            "+1555555010a", "+1 555 555 0101 ext", "' OR 1=1 --", "+1555555'0101",
                            "+15555550101; DROP TABLE users"}) {
        INFO("input: ", bad);
        CHECK_FALSE(validate::phone(bad));
    }
}

TEST_CASE("uuid format check")
{
    CHECK(validate::is_uuid("6b9bd7f0-4bbe-45af-882d-d29850c3037b"));
    CHECK_FALSE(validate::is_uuid("6b9bd7f0-4bbe-45af-882d-d29850c3037"));
    CHECK_FALSE(validate::is_uuid("6b9bd7f0x4bbe-45af-882d-d29850c3037b"));
    CHECK_FALSE(validate::is_uuid("' OR 1=1 --"));
    CHECK_FALSE(validate::is_uuid(""));
}

TEST_CASE("names and phones are masked for counterparties")
{
    CHECK(validate::mask_name("Alice Carter") == "Alice C.");
    CHECK(validate::mask_name("Mary Ann Smith") == "Mary S.");
    CHECK(validate::mask_name("Cher") == "Cher");
    CHECK(validate::mask_phone("+15555550101") == "***-***-0101");
    CHECK(validate::mask_phone("12") == "***");
}
