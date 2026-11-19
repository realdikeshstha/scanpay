#pragma once

#include <string>
#include <string_view>

namespace qr {

// Renders `text` as a standalone SVG QR code (error correction level M).
std::string render_svg(std::string_view text);

} // namespace qr
