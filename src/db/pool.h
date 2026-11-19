#pragma once

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <pqxx/pqxx>

namespace db {

// Fixed-size, thread-safe pool of PostgreSQL connections. Crow serves requests
// on several threads; each handler leases one connection for its work.
class Pool {
public:
    Pool(std::string url, std::size_t size);
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;

    class Lease {
    public:
        Lease(Pool& pool, std::unique_ptr<pqxx::connection> conn)
            : pool_(&pool), conn_(std::move(conn)) {}
        Lease(Lease&& other) noexcept
            : pool_(other.pool_), conn_(std::move(other.conn_)) {}
        Lease& operator=(Lease&&) = delete;
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        ~Lease();

        pqxx::connection& operator*() { return *conn_; }
        pqxx::connection* operator->() { return conn_.get(); }

    private:
        Pool* pool_;
        std::unique_ptr<pqxx::connection> conn_;
    };

    Lease acquire();

private:
    void release(std::unique_ptr<pqxx::connection> conn);
    std::unique_ptr<pqxx::connection> connect() const;

    std::string url_;
    std::mutex mutex_;
    std::condition_variable available_;
    std::vector<std::unique_ptr<pqxx::connection>> idle_;
};

// Every query in ScanPay goes through this helper: SQL text and values travel
// separately to PostgreSQL, so user input is never parsed as SQL.
template<typename... Args>
pqxx::result exec(pqxx::transaction_base& tx, pqxx::zview sql, Args&&... args)
{
    return tx.exec(sql, pqxx::params{std::forward<Args>(args)...});
}

} // namespace db
