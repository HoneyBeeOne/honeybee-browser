#include "browser/app.hpp"
#include <iostream>
#include <string>

namespace {

void printUsage() {
    std::cout << "HoneyBee Browser\n"
              << "A community-built web browser from Cairo University students.\n\n"
              << "Options:\n"
              << "  -v, --version  Show the browser version.\n"
              << "  -h, --help     Show this help message.\n";
}

} // namespace

int main(int argc, char* argv[]) {
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--version" || argument == "-v") {
            std::cout << "HoneyBee Browser v0.1.0\n";
            return 0;
        }
        if (argument == "--help" || argument == "-h") {
            printUsage();
            return 0;
        }
    }

    std::cout << "Starting GTK 4 application...\n";

    auto app = HoneyBeeApp();
    return app.run(argc, argv);
}
