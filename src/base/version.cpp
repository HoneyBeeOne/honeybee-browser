#include "base/version.hpp"

namespace honeybee {

std::string version_string() {
    return "0.4.0";
}

int version_major() { return 0; }
int version_minor() { return 4; }
int version_patch() { return 0; }

}  // namespace honeybee
