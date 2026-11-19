#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <iostream>

#include "crypto.h"
#include "fixture.h"

int main(int argc, char** argv)
{
    if (test::database_url().empty()) {
        std::cout << "SCANPAY_TEST_DATABASE_URL not set; skipping integration tests.\n";
        return 77;
    }
    crypto::init();
    doctest::Context ctx(argc, argv);
    return ctx.run();
}
