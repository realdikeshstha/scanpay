#include <iostream>

#include "config.h"
#include "crypto.h"
#include "security_log.h"
#include "web/routes.h"
#include "web/services.h"

int main()
{
    try {
        crypto::init();
        const auto cfg = config::Config::from_env();
        seclog::init(cfg.security_log_path);

        web::Services svc(cfg);
        web::App app;
        const bool tls = !cfg.tls_cert_path.empty();
        app.get_middleware<web::SecurityHeaders>().hsts = tls;
        web::register_routes(app, svc);

        // Never leak internals to the client.
        app.exception_handler([](crow::response& res) { res = crow::response(500, "Internal error"); });
        app.loglevel(crow::LogLevel::Warning);
        app.server_name("ScanPay");
        app.bindaddr(cfg.bind_address).port(cfg.port).multithreaded();

        if (tls) {
#ifdef CROW_ENABLE_SSL
            asio::ssl::context ctx(asio::ssl::context::tls_server);
            ctx.set_options(asio::ssl::context::default_workarounds | asio::ssl::context::no_sslv2
                            | asio::ssl::context::no_sslv3 | asio::ssl::context::no_tlsv1
                            | asio::ssl::context::no_tlsv1_1 | asio::ssl::context::single_dh_use);
            ctx.set_verify_mode(asio::ssl::verify_none);
            ctx.use_certificate_chain_file(cfg.tls_cert_path);
            ctx.use_private_key_file(cfg.tls_key_path, asio::ssl::context::pem);
            app.ssl(std::move(ctx));
#else
            std::cerr << "This build has no TLS support; rebuild with -DSCANPAY_SSL=ON\n";
            return 2;
#endif
        } else {
            std::cerr << "WARNING: running without TLS. Use only for local development.\n";
        }

        // cppcheck-suppress knownConditionTrueFalse ; depends on runtime environment
        std::cout << "ScanPay listening on " << (tls ? "https" : "http") << "://" << cfg.bind_address << ":"
                  << cfg.port << std::endl;
        // cppcheck-suppress knownConditionTrueFalse ; depends on runtime environment
        seclog::event("server_start", {{"tls", tls ? "on" : "off"}});
        app.run();
        return 0;
    } catch (const config::ConfigError& e) {
        std::cerr << "Configuration error: " << e.what() << '\n';
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << '\n';
        return 1;
    }
}
