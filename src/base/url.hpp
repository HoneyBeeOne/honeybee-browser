#pragma once

#include <string>

namespace honeybee {

// التحقق من أن النص يمثل رابطاً صحيحاً.
//
// الرابط الصحيح:
//   - يبدأ بأحد المخططات المدعومة: http، https، file.
//   - يحتوي على اسم مضيف (host) بعد البادئة.
//   - لا يحتوي على أي مسافة.
//
// أمثلة صحيحة: https://example.com، http://localhost:8080
// أمثلة خاطئة: example.com (بلا مخطط)، https:// (بلا مضيف)،
//              https://exa mple.com (به مسافة).
bool is_valid_url(const std::string& url);

// Resolve href relative to the current page's base URL (ADR-018).
// Absolute URLs (http://, https://, file://) pass through
// unchanged. Relative URLs prepend the base's directory (base
// with last path segment stripped, trailing '/' kept).
// Simplified resolution -- not full RFC 3986 -- but correct for
// the common cases: "/path" and "sibling.html".
//
// Used by both the CSS matcher (for :link/:visited URL
// resolution) and the browser layer (for click navigation),
// so the two agree on what URL a given href resolves to.
std::string resolve_url(const std::string& href, const std::string& base);

}  // namespace honeybee
