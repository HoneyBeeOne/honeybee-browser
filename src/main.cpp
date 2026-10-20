#include "browser/app.hpp"
#include <iostream>

#include "base/version.hpp"

int main(int argc, char* argv[]) {
    std::cout << "HoneyBee Browser v" << honeybee::version_string() << "\n";
    std::cout << "Starting GTK 4 application...\n";

    auto app = HoneyBeeApp();
    return app.run(argc, argv);
}
