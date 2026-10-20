#include "renderer/css/ua_stylesheet.hpp"

namespace honeybee::renderer::css {

namespace {

// ADR-014 scope of v1. Block elements only. font-weight and
// font-style are declared so the UA sheet matches CSS 2.1's
// intent; they do not flow into LayoutText yet (a future
// font-metrics ADR will consume them without touching this
// file).
constexpr const char* kUaCss = R"CSS(
body { margin: 8px }
h1 { font-size: 2em; font-weight: bold; margin: 0.67em 0 }
h2 { font-size: 1.5em; font-weight: bold; margin: 0.75em 0 }
h3 { font-size: 1.17em; font-weight: bold; margin: 0.83em 0 }
h4 { font-weight: bold; margin: 1.12em 0 }
h5 { font-size: 0.83em; font-weight: bold; margin: 1.5em 0 }
h6 { font-size: 0.67em; font-weight: bold; margin: 1.67em 0 }
p { margin: 1em 0 }
em, i, cite, var, address { font-style: italic }
strong, b { font-weight: bold }
code, pre, kbd, samp, tt { font-family: monospace }
ul, ol { margin: 1em 0; padding-left: 40px }
a { color: #0000EE }
)CSS";

} // namespace

const Stylesheet& ua_stylesheet() {
    static const Stylesheet sheet = Stylesheet::parse(kUaCss);
    return sheet;
}

} // namespace honeybee::renderer::css
