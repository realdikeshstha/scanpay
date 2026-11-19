#include "security_log.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace seclog {
namespace {

std::mutex g_mutex;
std::function<void(std::string_view)> g_writer = [](std::string_view line) { std::cerr << line << '\n'; };

void append_json_string(std::string& out, std::string_view s)
{
    static constexpr char kHex[] = "0123456789abcdef";
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out += kHex[c >> 4];
                    out += kHex[c & 0xF];
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

std::string utc_timestamp()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

} // namespace

void init(const std::string& path)
{
    if (path.empty()) return;
    auto file = std::make_shared<std::ofstream>(path, std::ios::app);
    if (!*file) throw std::runtime_error("cannot open security log file");
    set_writer([file](std::string_view line) {
        *file << line << '\n';
        file->flush();
        std::cerr << line << '\n';
    });
}

void set_writer(std::function<void(std::string_view)> writer)
{
    std::lock_guard lock(g_mutex);
    g_writer = std::move(writer);
}

void event(std::string_view name, std::initializer_list<Field> fields)
{
    std::string line = "{\"ts\":";
    append_json_string(line, utc_timestamp());
    line += ",\"event\":";
    append_json_string(line, name);
    for (const auto& [key, value] : fields) {
        line += ',';
        append_json_string(line, key);
        line += ':';
        append_json_string(line, value);
    }
    line += '}';

    std::lock_guard lock(g_mutex);
    if (g_writer) g_writer(line);
}

} // namespace seclog
