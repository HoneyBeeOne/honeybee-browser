#include "net/client.hpp"

#include <curl/curl.h>

#include <cstddef>
#include <memory>

namespace honeybee::net {

namespace {

// RAII: مُدير موارد libcurl.
//
// نستخدم std::unique_ptr مع deleter مخصص، حتى يُحرَّر المقبض
// تلقائياً عند الخروج من الدالة، حتى لو حدث استثناء أو عودة
// مبكرة. هذا يمنع تسرّب الموارد (memory leak).
struct CurlDeleter {
    void operator()(CURL* handle) const {
        if (handle != nullptr) {
            curl_easy_cleanup(handle);
        }
    }
};

using CurlHandle = std::unique_ptr<CURL, CurlDeleter>;

// callback يجمع محتوى الرد في std::string.
//
// يُستدعى من libcurl عدة مرات أثناء وصول البيانات. كل مرة
// يحصل على قطعة (chunk) من المحتوى، ويُضيفها إلى النص الكامل.
//
// المعاملات:
//   - contents: مؤشر إلى البيانات الواردة.
//   - size: حجم العنصر الواحد (عادة 1).
//   - nmemb: عدد العناصر.
//   - userp: مؤشر إلى البيانات التي مررناها نحن (std::string).
//
// القيمة المُعادة: عدد البايتات المُعالَجة. لو أعدنا رقماً
// مخالفاً، يعتبر libcurl أن هناك خطأ.
std::size_t write_callback(char* contents, std::size_t size,
                           std::size_t nmemb, void* userp) {
    const std::size_t total = size * nmemb;
    auto* output = static_cast<std::string*>(userp);
    output->append(contents, total);
    return total;
}

// callback يجمع ترويسات الرد في std::map.
//
// كل ترويسة تأتي في سطر منفصل بالصيغة: "Name: Value\r\n".
// نتجاهل السطور التي لا تحتوي على نقطتين (:)، مثل سطر الحالة
// الأول "HTTP/1.1 200 OK" والسطر الفارغ في نهاية الترويسات.
std::size_t header_callback(char* buffer, std::size_t size,
                            std::size_t nitems, void* userp) {
    const std::size_t total = size * nitems;
    auto* headers =
        static_cast<std::map<std::string, std::string>*>(userp);

    std::string line(buffer, total);

    // إزالة "\r\n" من النهاية إن وُجدا.
    while (!line.empty() &&
           (line.back() == '\r' || line.back() == '\n')) {
        line.pop_back();
    }

    // تجاهل السطور الفارغة أو سطور الحالة.
    const auto colon = line.find(':');
    if (colon == std::string::npos) {
        return total;
    }

    std::string name = line.substr(0, colon);
    std::string value = line.substr(colon + 1);

    // إزالة المسافات البادئة من القيمة.
    while (!value.empty() && value.front() == ' ') {
        value.erase(value.begin());
    }

    (*headers)[name] = value;
    return total;
}

}  // namespace

Response fetch(const std::string& url) {
    Response response;

    // تهيئة libcurl العامة. يجب استدعاؤها مرة واحدة على الأقل
    // قبل استخدام أي دالة أخرى. النتيجة غير مهمة في المرحلة
    // الحالية، لأن curl_easy_init سيتولى التهيئة إن لزم.
    curl_global_init(CURL_GLOBAL_DEFAULT);

    CurlHandle handle(curl_easy_init());
    if (!handle) {
        return response;  // فشل التهيئة.
    }

    // ضبط الرابط، والـ callbacks، ووجهاتها.
    curl_easy_setopt(handle.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION,
                     write_callback);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA,
                     &response.body);
    curl_easy_setopt(handle.get(), CURLOPT_HEADERFUNCTION,
                     header_callback);
    curl_easy_setopt(handle.get(), CURLOPT_HEADERDATA,
                     &response.headers);

    // اتبع إعادة التوجيه (301، 302، ...).
    curl_easy_setopt(handle.get(), CURLOPT_FOLLOWLOCATION, 1L);

    // مهلة الاتصال: 10 ثوانٍ. تمنع التعليق الطويل عند خادم
    // لا يستجيب.
    curl_easy_setopt(handle.get(), CURLOPT_CONNECTTIMEOUT, 3L);

    // تنفيذ الطلب.
    const CURLcode result = curl_easy_perform(handle.get());
    if (result != CURLE_OK) {
        // فشل الطلب. نُبقي رمز الحالة على صفر.
        response.body.clear();
        response.headers.clear();
        return response;
    }

    // استخراج رمز الحالة من libcurl.
    long status = 0;
    curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &status);
    response.status_code = static_cast<int>(status);

    return response;
}

}  // namespace honeybee::net
