#pragma once

#include <string>

#include "net/response.hpp"

namespace honeybee::net {

// جلب مورد من الشبكة عبر بروتوكول HTTP أو HTTPS.
//
// المدخل: رابط كامل (يبدأ بـ http:// أو https://).
// المخرج: بنية Response تحتوي على رمز الحالة، والترويسات،
//         والمحتوى.
//
// في حال فشل الطلب (خطأ شبكة، رابط خاطئ، إلخ)، تُعاد بنية
// Response برمز حالة صفر ومحتوى فارغ.
//
// ملاحظة: هذه الدالة متزامنة (synchronous). أي أنها تحجب
// الخيط الحالي حتى ينتهي الطلب. هذا مقبول في المرحلة الحالية،
// وسننتقل إلى الطلبات اللاتزامنية لاحقاً عند بناء العمليات
// المتعددة (Phase 4).
Response fetch(const std::string& url);

}  // namespace honeybee::net
