#pragma once

#include <functional>
#include <string>
#include <thread>

#include <glibmm/dispatcher.h>

#include "net/response.hpp"

// يجلب الموارد من الشبكة في خيط منفصل، دون حجب الخيط الرئيسي.
//
// الاستخدام:
//   AsyncFetcher fetcher;
//   fetcher.fetch("https://example.com",
//                 [](honeybee::net::Response r) {
//                     // يُنفَّذ هذا الرد في الخيط الرئيسي.
//                 });
//
// قيود مهمة:
//   - لا يُسمح باستدعاء fetch() قبل أن ينتهي الجلب السابق.
//     يُنصح بتعطيل عنصر الواجهة الذي يُطلق الجلب، وإعادة تفعيله
//     في الرد.
//   - الصنف ليس آمناً للاستخدام من عدة خيوط في وقت واحد.
//
// كيف يعمل؟
//   - عند استدعاء fetch()، يُنشَأ خيط جديد يشغّل net::fetch().
//   - عند انتهاء الجلب، يُبلِّغ الخيط الرئيسي عبر Glib::Dispatcher.
//   - يُستدعى الرد المُسجَّل في الخيط الرئيسي، حيث حلقلة GTK.
class AsyncFetcher {
public:
    using Callback =
        std::function<void(honeybee::net::Response)>;

    AsyncFetcher();
    ~AsyncFetcher();

    // لا يُنسخ ولا يُنقل.
    AsyncFetcher(const AsyncFetcher&) = delete;
    AsyncFetcher& operator=(const AsyncFetcher&) = delete;

    // يبدأ جلب الرابط في خيط منفصل.
    // عند الانتهاء، يُستدعى on_done في الخيط الرئيسي.
    void fetch(const std::string& url, Callback on_done);

private:
    Glib::Dispatcher m_dispatcher;  // يُبلِّغ الخيط الرئيسي
    honeybee::net::Response m_result;  // نتيجة الجلب الأخيرة
    Callback m_callback;               // الرد المُستدعى عند الانتهاء
    std::thread m_thread;              // خيط الجلب
};
