#include "auth/session.h"

#include "crypto.h"
#include "security_log.h"

namespace auth {
namespace {

constexpr std::size_t kTokenBytes = 32;
constexpr std::size_t kMaxTokenLength = 64;

std::int64_t seconds(std::chrono::seconds s) { return static_cast<std::int64_t>(s.count()); }

} // namespace

NewSession SessionStore::create(std::int64_t user_id)
{
    NewSession out;
    out.token = crypto::random_token(kTokenBytes);
    out.session.token_hash = crypto::sha256_hex(out.token);
    out.session.csrf_token = crypto::random_token(kTokenBytes);

    auto conn = pool_.acquire();
    pqxx::work tx(*conn);
    // Opportunistic cleanup of this user's stale sessions.
    db::exec(tx,
             "DELETE FROM sessions WHERE user_id = $1 AND "
             "(last_seen < now() - make_interval(secs => $2) OR created_at < now() - make_interval(secs => $3))",
             user_id, seconds(kIdleTimeout), seconds(kAbsoluteLifetime));
    auto row = db::exec(tx,
                        "WITH s AS (INSERT INTO sessions (token_hash, user_id, csrf_token) VALUES ($1, $2, $3) "
                        "RETURNING user_id) "
                        "SELECT u.full_name, u.phone, u.role FROM s JOIN users u ON u.id = s.user_id",
                        out.session.token_hash, user_id, out.session.csrf_token)
                   .one_row();
    tx.commit();

    out.session.user_id = user_id;
    out.session.full_name = row[0].as<std::string>();
    out.session.phone = row[1].as<std::string>();
    out.session.role = row[2].as<std::string>();
    return out;
}

std::optional<Session> SessionStore::lookup(std::string_view token)
{
    if (token.empty() || token.size() > kMaxTokenLength) return std::nullopt;
    const std::string hash = crypto::sha256_hex(token);

    auto conn = pool_.acquire();
    pqxx::work tx(*conn);
    auto rows = db::exec(tx,
                         "WITH s AS ("
                         "  UPDATE sessions SET last_seen = now() "
                         "  WHERE token_hash = $1 "
                         "    AND last_seen >= now() - make_interval(secs => $2) "
                         "    AND created_at >= now() - make_interval(secs => $3) "
                         "  RETURNING user_id, csrf_token) "
                         "SELECT s.user_id, s.csrf_token, u.full_name, u.phone, u.role "
                         "FROM s JOIN users u ON u.id = s.user_id",
                         hash, seconds(kIdleTimeout), seconds(kAbsoluteLifetime));
    if (rows.empty()) {
        auto deleted = db::exec(tx, "DELETE FROM sessions WHERE token_hash = $1 RETURNING user_id", hash);
        tx.commit();
        if (!deleted.empty())
            seclog::event("session_expired", {{"user_id", deleted[0][0].as<std::string>()}});
        return std::nullopt;
    }
    tx.commit();

    Session s;
    s.user_id = rows[0][0].as<std::int64_t>();
    s.token_hash = hash;
    s.csrf_token = rows[0][1].as<std::string>();
    s.full_name = rows[0][2].as<std::string>();
    s.phone = rows[0][3].as<std::string>();
    s.role = rows[0][4].as<std::string>();
    return s;
}

void SessionStore::destroy(std::string_view token)
{
    if (token.empty() || token.size() > kMaxTokenLength) return;
    auto conn = pool_.acquire();
    pqxx::work tx(*conn);
    db::exec(tx, "DELETE FROM sessions WHERE token_hash = $1", crypto::sha256_hex(token));
    tx.commit();
}

} // namespace auth
