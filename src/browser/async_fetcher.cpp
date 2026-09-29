#include "browser/async_fetcher.hpp"

#include <utility>

#include "net/client.hpp"


AsyncFetcher::AsyncFetcher() {
    // نربط الإشارة بدالة تُستدعى في الخيط الرئيسي.
    // نستخدم مؤشر this لالتقاط الحالة العضو.
    m_dispatcher.connect([this]() {
        if (m_callback) {
            m_callback(m_result);
        }
    });
}

AsyncFetcher::~AsyncFetcher() {
    // ننتظر انتهاء الخيط قبل التدمير، لتفادي تدمير كائن
    // لا يزال الخيط يستخدمه.
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void AsyncFetcher::fetch(const std::string& url, Callback on_done) {
    // لو كان هناك خيط سابق، ننتظر انتهاءه.
    // هذا يمنع تشغيل جلبتين في وقت واحد.
    if (m_thread.joinable()) {
        m_thread.join();
    }

    // نحفظ الرد الجديد، ليستخدمه الـ Dispatcher عند الإطلاق.
    m_callback = std::move(on_done);

    // نُشغّل الجلب في خيط جديد.
    // الخيط يلتقط نسخة من الرابط (by value)، لأن الرابط قد
    // ينتهي عمره في الخيط الرئيسي قبل انتهاء الجلب.
    m_thread = std::thread([this, url]() {
        m_result = honeybee::net::fetch(url);
        // نُبلغ الخيط الرئيسي. الـ Dispatcher يتولى الجدولة.
        m_dispatcher.emit();
    });
}
