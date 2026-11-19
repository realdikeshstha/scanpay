#pragma once

#include <map>
#include <string>

#include "crow.h"

namespace web {

// Loads every template once at startup and serves them (including partials)
// from memory. Pages are rendered with Crow's Mustache engine, whose {{ }}
// tags HTML-escape every value. Templates never use unescaped {{{ }}} tags.
class Views {
public:
    explicit Views(const std::string& directory);

    std::string render(const std::string& name, const crow::mustache::context& ctx) const;

private:
    std::map<std::string, crow::mustache::template_t> compiled_;
};

} // namespace web
