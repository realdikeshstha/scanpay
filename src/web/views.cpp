#include "web/views.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>

namespace web {

Views::Views(const std::string& directory)
{
    namespace fs = std::filesystem;
    auto sources = std::make_shared<std::map<std::string, std::string>>();
    if (!fs::is_directory(directory)) throw std::runtime_error("template directory not found: " + directory);
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".html") continue;
        std::ifstream in(entry.path());
        (*sources)[entry.path().filename().string()] =
            std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    // Partials ({{> _header.html}}) are resolved through this loader, which
    // only ever returns templates read above.
    crow::mustache::set_loader([sources](const std::string& name) -> std::string {
        auto it = sources->find(name);
        return it == sources->end() ? std::string{} : it->second;
    });

    for (const auto& [name, text] : *sources) compiled_.emplace(name, crow::mustache::compile(text));
}

std::string Views::render(const std::string& name, const crow::mustache::context& ctx) const
{
    auto it = compiled_.find(name);
    if (it == compiled_.end()) throw std::runtime_error("unknown template: " + name);
    return it->second.render_string(ctx);
}

} // namespace web
