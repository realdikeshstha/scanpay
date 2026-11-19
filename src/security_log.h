#pragma once

#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

// Structured (JSON lines) security event log.
//
// Callers must never pass passwords, session tokens, CSRF tokens, cookies or
// full phone numbers; use validate::mask_phone for phones.
namespace seclog {

using Field = std::pair<std::string_view, std::string>;

// Empty path logs to stderr.
void init(const std::string& path);

// Replaces the output sink (tests use this to capture events).
void set_writer(std::function<void(std::string_view line)> writer);

void event(std::string_view name, std::initializer_list<Field> fields = {});

} // namespace seclog
