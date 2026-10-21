#pragma once

#include <string>

namespace honeybee {

// إرجاع رقم الإصدار كنص، مثال: "0.3.0"
std::string version_string();

// مكونات رقم الإصدار منفصلة
int version_major();
int version_minor();
int version_patch();

}  // namespace honeybee
