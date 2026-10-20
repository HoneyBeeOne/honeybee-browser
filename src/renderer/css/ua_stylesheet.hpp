// =============================================================
// File: ua_stylesheet.hpp
// Purpose: The user-agent stylesheet (per ADR-014).
//
// Returns a reference to a process-lifetime Stylesheet parsed
// once from the built-in CSS. The reference is stable: callers
// can store it or compare addresses across calls.
// =============================================================

#pragma once

#include "renderer/css/stylesheet.hpp"

namespace honeybee::renderer::css {

const Stylesheet& ua_stylesheet();

} // namespace honeybee::renderer::css
