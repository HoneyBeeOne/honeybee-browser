#pragma once

#include <map>
#include <string>

namespace honeybee::net {

// نتيجة جلب مورد من الشبكة.
//
// تحتوي هذه البنية على كل ما يُهمّ من رد الخادم:
//   - رمز الحالة (200، 404، إلخ).
//   - ترويسات الرد (Content-Type، Content-Length، إلخ).
//   - محتوى الرد (نص HTML، صورة، أو أي شيء آخر).
//
// القيمة الافتراضية لرمز الحالة هي صفر، وتدل على أن الطلب فشل
// قبل أن يصل إلى الخادم (خطأ في الشبكة، رابط غير صالح، إلخ).
struct Response {
    int status_code = 0;
    std::string body;
    std::map<std::string, std::string> headers;

    // هل الطلب نجح؟ أي أن رمز الحالة في نطاق 2xx.
    bool success() const {
        return status_code >= 200 && status_code < 300;
    }
};

}  // namespace honeybee::net
