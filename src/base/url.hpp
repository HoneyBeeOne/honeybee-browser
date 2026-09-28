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

}  // namespace honeybee
