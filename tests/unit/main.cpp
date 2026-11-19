#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "crypto.h"
#include "security_log.h"

int main(int argc, char** argv)
{
    crypto::init();
    seclog::set_writer([](std::string_view) {});
    doctest::Context ctx(argc, argv);
    return ctx.run();
}
