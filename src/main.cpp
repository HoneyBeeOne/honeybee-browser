#include "browser/app.hpp"
#include <iostream>

#include "base/log.hpp"
#include "base/version.hpp"

int main(int argc, char* argv[]) {
    // ADR-046: read HONEYBEE_LOG / _LEVEL / _FILE env vars.
    // No-op when HONEYBEE_LOG is unset.
    honeybee::log_init();
    HB_LOG(Pipeline, Info, "startup: version=%s",
           honeybee::version_string().c_str());

    std::cout << "HoneyBee Browser v" << honeybee::version_string() << "\n";
    std::cout << "Starting GTK 4 application...\n";

    auto app = HoneyBeeApp();
    return app.run(argc, argv);
}
