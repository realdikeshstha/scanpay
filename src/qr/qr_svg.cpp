#include "qr/qr_svg.h"

#include <qrcodegen.hpp>

namespace qr {

std::string render_svg(std::string_view text)
{
    using qrcodegen::QrCode;
    const QrCode code = QrCode::encodeText(std::string(text).c_str(), QrCode::Ecc::MEDIUM);
    constexpr int border = 4;
    const int size = code.getSize() + border * 2;
    const std::string dim = std::to_string(size);

    std::string svg;
    svg += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    svg += "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" viewBox=\"0 0 " + dim + " " + dim
           + "\" stroke=\"none\" shape-rendering=\"crispEdges\">\n";
    svg += "<rect width=\"100%\" height=\"100%\" fill=\"#FFFFFF\"/>\n<path d=\"";
    for (int y = 0; y < code.getSize(); ++y) {
        for (int x = 0; x < code.getSize(); ++x) {
            if (!code.getModule(x, y)) continue;
            svg += "M" + std::to_string(x + border) + "," + std::to_string(y + border) + "h1v1h-1z";
        }
    }
    svg += "\" fill=\"#000000\"/>\n</svg>\n";
    return svg;
}

} // namespace qr
