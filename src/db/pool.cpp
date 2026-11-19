#include "db/pool.h"

#include <stdexcept>

namespace db {

Pool::Pool(std::string url, std::size_t size) : url_(std::move(url))
{
    if (size == 0) throw std::invalid_argument("database pool size must be positive");
    idle_.reserve(size);
    for (std::size_t i = 0; i < size; ++i) idle_.push_back(connect());
}

std::unique_ptr<pqxx::connection> Pool::connect() const
{
    return std::make_unique<pqxx::connection>(url_);
}

Pool::Lease Pool::acquire()
{
    std::unique_ptr<pqxx::connection> conn;
    {
        std::unique_lock lock(mutex_);
        available_.wait(lock, [this] { return !idle_.empty(); });
        conn = std::move(idle_.back());
        idle_.pop_back();
    }
    if (!conn || !conn->is_open()) {
        try {
            conn = connect();
        } catch (...) {
            release(nullptr);
            throw;
        }
    }
    return Lease(*this, std::move(conn));
}

void Pool::release(std::unique_ptr<pqxx::connection> conn)
{
    {
        std::lock_guard lock(mutex_);
        // A null slot is reconnected lazily by the next acquire().
        idle_.push_back(std::move(conn));
    }
    available_.notify_one();
}

Pool::Lease::~Lease()
{
    if (pool_ && conn_) pool_->release(std::move(conn_));
}

} // namespace db
