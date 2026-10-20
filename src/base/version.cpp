#include "base/version.hpp"

namespace honeybee {

std::string version_string() {
    return "0.2.1";
}

int version_major() { return 0; }
int version_minor() { return 2; }
int version_patch() { return 1; }

}  // namespace honeybee
