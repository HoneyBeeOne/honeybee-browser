#include "browser/app.hpp"
#include <cstdlib>
#include <iostream>

#include "base/log.hpp"
#include "base/version.hpp"

int main(int argc, char* argv[]) {
    // ADR-068: GTK4's GSK GPU renderer crashes on very large
    // cairo surfaces (empirically >~25,000px tall) with
    // corrupted clip state during GPU upload. HoneyBee paints
    // the full ~30,000px Wikipedia article into a single tall
    // DrawingArea (ADR-021 / ADR-064), which triggers the bug.
    //
    // The Cairo CPU renderer bypasses GPU texture upload
    // entirely and works cleanly for the same content.
    //
    // overwrite=0 preserves any user-set GSK_RENDERER so they
    // can opt back into GPU if a GTK fix lands
    // (e.g., GSK_RENDERER=gl) or try alternatives (vulkan, nop).
    setenv("GSK_RENDERER", "cairo", 0);

    // ADR-046: read HONEYBEE_LOG / _LEVEL / _FILE env vars.
    // No-op when HONEYBEE_LOG is unset.
    honeybee::log_init();
    HB_LOG(Pipeline, Info, "startup: version=%s",
           honeybee::version_string().c_str());

    std::cout << "HoneyBee Browser v" << honeybee::version_string() << "\n";
    std::cout << "Starting GTK 4 application...\n";

    auto app = HoneyBeeApp();
    return app.run(argc, argv);
}
