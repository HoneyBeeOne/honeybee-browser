#pragma once

#include <iostream>
#include <string>
#include <string_view>

namespace honeybee {

inline bool handle_cli_options(int argc, char* argv[], std::ostream& output,
                               const std::string& version) {
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--version" || argument == "-v") {
            output << "HoneyBee Browser v" << version << '\n';
            return true;
        }
        if (argument == "--help" || argument == "-h") {
            output << "HoneyBee Browser\n"
                   << "A community-built web browser from Cairo University students.\n\n"
                   << "Options:\n"
                   << "  -v, --version  Show the browser version.\n"
                   << "  -h, --help     Show this help message.\n";
            return true;
        }
    }

    return false;
}

}  // namespace honeybee
