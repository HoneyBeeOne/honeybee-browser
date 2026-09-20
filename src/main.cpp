#include "browser/app.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    std::cout << "HoneyBee Browser v0.1.0\n";
    std::cout << "Starting GTK 4 application...\n";

    auto app = HoneyBeeApp();
    return app.run(argc, argv);
}
