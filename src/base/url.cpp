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
    // ADR-035-prep: protocol-relative URLs (e.g. "//host/path")
    // inherit the scheme from the base. Without this, they fall
    // through to relative resolution and become
    // "https://wiki.org//host/path" (double slash, wrong host).
    if (href.size() >= 2 && href[0] == '/' && href[1] == '/') {
        if (base.rfind("http://", 0) == 0) return "http:" + href;
        return "https:" + href;  // default (also for file:// or schemeless base)
    }

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

    if (href[0] == '/') {
        // Root-relative URL (e.g. "/w/load.php"): resolve against
        // the ORIGIN (scheme + host), not the base directory.
        // Per RFC 3986 §5.2.2, "/path" replaces the entire path
        // component of the base URL.
        //
        // Prior to ADR-028 this branch concatenated onto `dir`
        // (base with last segment stripped), which is correct for
        // bases without subdirectories ("https://example.com/page"
        // -> dir = "https://example.com/") but wrong for bases
        // with subdirectories ("https://en.wikipedia.org/wiki/HTML"
        // -> dir = "https://en.wikipedia.org/wiki/", producing
        // "https://en.wikipedia.org/wiki/w/load.php" when it should
        // be "https://en.wikipedia.org/w/load.php").
        //
        // Wikipedia exposes this bug because its base URL has a
        // "/wiki/" subdirectory, and its external CSS/JS hrefs are
        // root-relative ("/w/load.php"). Every external CSS fetch
        // was silently 404ing, so external CSS never actually
        // reached the cascade. That's why ADR-025/026/027's
        // "external CSS loading" was really inline <style> doing
        // the work, and why ADR-028's grid rules (external-only)
        // never fired.
        const auto scheme_end = dir.find("://");
        if (scheme_end == std::string::npos) return dir + href;
        const auto host_end = dir.find('/', scheme_end + 3);
        const std::string origin =
            (host_end == std::string::npos)
                ? dir.substr(0, dir.size() - 1)  // strip trailing '/'
                : dir.substr(0, host_end);
        return origin + href;
    }
    return dir + href;
}

}  // namespace honeybee
