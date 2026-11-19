#include "web/services.h"

namespace web {
namespace {

constexpr std::size_t kLoginAttemptsPerIp = 20;
constexpr std::chrono::seconds kLoginWindow = std::chrono::minutes(10);
constexpr std::size_t kLookupsPerWindow = 10;
constexpr std::chrono::seconds kLookupWindow = std::chrono::minutes(1);

} // namespace

Services::Services(const config::Config& c)
    : cfg(c),
      pool(cfg.database_url, cfg.db_pool_size),
      sessions(pool),
      login_limiter(kLoginAttemptsPerIp, kLoginWindow),
      lookup_limiter(kLookupsPerWindow, kLookupWindow),
      login(pool, sessions, login_limiter),
      signer(cfg.qr_key),
      authorizations(pool),
      transfers(pool),
      history(pool),
      views(cfg.template_dir)
{
}

} // namespace web
