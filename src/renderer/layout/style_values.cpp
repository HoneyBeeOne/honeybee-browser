#include "renderer/layout/style_values.hpp"

#include <cctype>
#include <string>
#include <string_view>
#include <unordered_map>

namespace honeybee::renderer::layout {

namespace {

bool is_ws(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && is_ws(s.front())) s.remove_prefix(1);
    while (!s.empty() && is_ws(s.back())) s.remove_suffix(1);
    return s;
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool has_internal_ws(std::string_view s) {
    for (char c : s) if (is_ws(c)) return true;
    return false;
}

bool ends_with(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() &&
           s.substr(s.size() - suffix.size()) == suffix;
}

} // namespace

float parse_px(std::string_view s) {
    s = trim(s);
    if (s.empty()) return 0.0f;
    if (has_internal_ws(s)) return 0.0f;
    if (s.size() < 3) return 0.0f;
    if (!ends_with(s, "px")) return 0.0f;
    try {
        return std::stof(std::string(s.substr(0, s.size() - 2)));
    } catch (...) {
        return 0.0f;
    }
}

float parse_length(std::string_view s, const LengthContext& ctx) {
    s = trim(s);
    if (s.empty()) return 0.0f;
    if (has_internal_ws(s)) return 0.0f;
    if (s == "auto") return 0.0f;

    enum Unit { PX, EM, REM, PCT } unit;
    size_t suffix_len = 0;

    if (ends_with(s, "px")) {
        unit = PX; suffix_len = 2;
    } else if (ends_with(s, "rem")) {
        unit = REM; suffix_len = 3;
    } else if (ends_with(s, "em")) {
        unit = EM; suffix_len = 2;
    } else if (ends_with(s, "%")) {
        unit = PCT; suffix_len = 1;
    } else {
        return 0.0f;
    }

    if (s.size() <= suffix_len) return 0.0f;

    float n = 0.0f;
    try {
        n = std::stof(std::string(s.substr(0, s.size() - suffix_len)));
    } catch (...) {
        return 0.0f;
    }

    switch (unit) {
        case PX:
            return n;
        case EM: {
            const float ref = ctx.on_font_size_property
                                  ? ctx.parent_font_size
                                  : ctx.font_size;
            return n * ref;
        }
        case REM:
            return n * ctx.root_font_size;
        case PCT:
            return (n / 100.0f) * ctx.parent_content_width;
    }
    return 0.0f;
}

bool is_auto(std::string_view s) {
    return trim(s) == "auto";
}

Color parse_color(std::string_view s) {
    s = trim(s);
    const std::string key(s);

    static const std::unordered_map<std::string, Color> named = {
        {"transparent", Color::transparent()},
        {"black",       {0.0f, 0.0f, 0.0f, 1.0f}},
        {"white",       {1.0f, 1.0f, 1.0f, 1.0f}},
        {"red",         {1.0f, 0.0f, 0.0f, 1.0f}},
        {"green",       {0.0f, 0.5f, 0.0f, 1.0f}},
        {"blue",        {0.0f, 0.0f, 1.0f, 1.0f}},
        {"navy",        {0.0f, 0.0f, 0.502f, 1.0f}},
        {"yellow",      {1.0f, 1.0f, 0.0f, 1.0f}},
        {"orange",      {1.0f, 0.647f, 0.0f, 1.0f}},
        {"purple",      {0.502f, 0.0f, 0.502f, 1.0f}},
        {"gray",        {0.5f, 0.5f, 0.5f, 1.0f}},
        {"grey",        {0.5f, 0.5f, 0.5f, 1.0f}},
        {"silver",      {0.753f, 0.753f, 0.753f, 1.0f}},
        {"maroon",      {0.502f, 0.0f, 0.0f, 1.0f}},
        {"teal",        {0.0f, 0.502f, 0.502f, 1.0f}},
        {"aqua",        {0.0f, 1.0f, 1.0f, 1.0f}},
        {"fuchsia",     {1.0f, 0.0f, 1.0f, 1.0f}},
        {"lime",        {0.0f, 1.0f, 0.0f, 1.0f}},
        {"olive",       {0.502f, 0.502f, 0.0f, 1.0f}},
    };
    auto it = named.find(key);
    if (it != named.end()) return it->second;

    if (!key.empty() && key[0] == '#') {
        const std::string hex = key.substr(1);
        if (hex.size() == 3) {
            const int r = hex_digit(hex[0]);
            const int g = hex_digit(hex[1]);
            const int b = hex_digit(hex[2]);
            if (r >= 0 && g >= 0 && b >= 0) {
                return {r / 15.0f, g / 15.0f, b / 15.0f, 1.0f};
            }
        } else if (hex.size() == 6) {
            const int r = hex_digit(hex[0]) * 16 + hex_digit(hex[1]);
            const int g = hex_digit(hex[2]) * 16 + hex_digit(hex[3]);
            const int b = hex_digit(hex[4]) * 16 + hex_digit(hex[5]);
            if (r >= 0 && g >= 0 && b >= 0) {
                return {r / 255.0f, g / 255.0f, b / 255.0f, 1.0f};
            }
        }
    }

    return Color::transparent();
}

} // namespace honeybee::renderer::layout
