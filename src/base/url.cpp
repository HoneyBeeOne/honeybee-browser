#include "base/url.hpp"

namespace honeybee {

bool is_valid_url(const std::string& url) {
    // 1. النص الفارغ غير مقبول.
    if (url.empty()) {
        return false;
    }

    // 2. الروابط لا تحتوي على مسافات.
    if (url.find(' ') != std::string::npos) {
        return false;
    }

    // 3. البحث عن مخطط مدعوم في بداية النص.
    //    المخططات المدعومة: http، https، file.
    std::string scheme;
    if (url.rfind("https://", 0) == 0) {
        scheme = "https://";
    } else if (url.rfind("http://", 0) == 0) {
        scheme = "http://";
    } else if (url.rfind("file://", 0) == 0) {
        scheme = "file://";
    } else {
        return false;
    }

    // 4. يجب أن يوجد شيء بعد المخطط.
    const std::string rest = url.substr(scheme.length());
    if (rest.empty()) {
        return false;
    }

    // 5. في http و https، لا يمكن أن يبدأ اسم المضيف بشرطة مائلة.
    //    لكن في file، الشكل القياسي هو file:///path، وفيه ثلاث
    //    شرطات مائلة، فلا نمنعها.
    if (scheme != "file://" && rest[0] == '/') {
        return false;
    }

    return true;
}

}  // namespace honeybee
