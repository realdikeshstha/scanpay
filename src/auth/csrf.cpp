#include "auth/csrf.h"

#include "crypto.h"

namespace auth {

bool csrf_matches(std::string_view expected, std::string_view submitted)
{
    if (expected.empty() || submitted.empty()) return false;
    return crypto::constant_time_equal(expected, submitted);
}

} // namespace auth
