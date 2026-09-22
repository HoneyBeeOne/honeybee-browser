#include "base/version.hpp"

namespace honeybee {

std::string version_string() {
    return "0.1.0";
}

int version_major() { return 0; }
int version_minor() { return 1; }
int version_patch() { return 0; }

}  // namespace honeybee
