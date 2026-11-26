#include <catch2/catch_test_macros.hpp>

#include <sstream>

#include "browser/cli.hpp"

TEST_CASE("CLI version options print the current version and exit") {
    char long_option[] = "--version";
    char short_option[] = "-v";
    for (char* option : {long_option, short_option}) {
        char program[] = "honeybee";
        char* arguments[] = {program, option};
        std::ostringstream output;

        REQUIRE(honeybee::handle_cli_options(2, arguments, output, "0.4.0"));
        REQUIRE(output.str() == "HoneyBee Browser v0.4.0\n");
    }
}

TEST_CASE("CLI help options print usage and exit") {
    char long_option[] = "--help";
    char short_option[] = "-h";
    for (char* option : {long_option, short_option}) {
        char program[] = "honeybee";
        char* arguments[] = {program, option};
        std::ostringstream output;

        REQUIRE(honeybee::handle_cli_options(2, arguments, output, "0.4.0"));
        REQUIRE(output.str().find("-v, --version") != std::string::npos);
        REQUIRE(output.str().find("-h, --help") != std::string::npos);
    }
}

TEST_CASE("CLI returns false when no recognized option is present") {
    char program[] = "honeybee";
    char page[] = "https://example.com";
    char* arguments[] = {program, page};
    std::ostringstream output;

    REQUIRE_FALSE(honeybee::handle_cli_options(2, arguments, output, "0.4.0"));
    REQUIRE(output.str().empty());
}
