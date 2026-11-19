#pragma once

#include "crow.h"
#include "web/guard.h"
#include "web/services.h"

namespace web {

using App = crow::App<SecurityHeaders>;

void register_routes(App& app, Services& svc);

} // namespace web
