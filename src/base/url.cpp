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

std::string resolve_url(const std::string& href,
                         const std::string& base) {
    // Absolute URLs: pass through.
    if (href.size() >= 7 && href.compare(0, 7, "http://") == 0) return href;
    if (href.size() >= 8 && href.compare(0, 8, "https://") == 0) return href;
    if (href.size() >= 7 && href.compare(0, 7, "file://") == 0) return href;
    if (base.empty()) return href;
    if (href.empty()) return base;

    // Compute base directory: strip query/fragment, strip the
    // last path segment (keep the trailing '/').
    std::string dir = base;
    const auto q = dir.find_first_of("?#");
    if (q != std::string::npos) dir.resize(q);
    const auto scheme_end = dir.find("://");
    const auto last_slash = dir.rfind('/');
    if (last_slash != std::string::npos
        && (scheme_end == std::string::npos
            || last_slash > scheme_end + 2)) {
        dir.resize(last_slash + 1);  // keep trailing '/'
    } else if (scheme_end != std::string::npos) {
        dir.push_back('/');  // "http://example.com" -> "...com/"
    }

    if (href[0] == '/') return dir + href.substr(1);
    return dir + href;
}

}  // namespace honeybee
