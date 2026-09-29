#pragma once

#include <gtkmm.h>

#include "browser/async_fetcher.hpp"

class HoneyBeeWindow : public Gtk::Window {
public:
    HoneyBeeWindow();

private:
    // معالجة ضغط Enter في شريط العنوان.
    // تتحقق من الرابط، وتبدأ جلبه في الخيط الخلفي.
    void on_url_activated();

    Gtk::Box m_root_box;            // الحاوية الرئيسية: تخطيط عمودي
    Gtk::Box m_toolbar_box;         // شريط الأدوات: تخطيط أفقي
    Gtk::Button m_back_button;      // زر الرجوع
    Gtk::Button m_forward_button;   // زر التقدم
    Gtk::Button m_reload_button;    // زر إعادة التحميل
    Gtk::Entry m_url_bar;           // شريط العنوان
    Gtk::Label m_label;             // النص المؤقت في وسط النافذة
    Gtk::Box m_status_bar;          // شريط الحالة: تخطيط أفقي
    Gtk::Label m_status_label;      // نص الحالة
    Gtk::Label m_version_label;     // رقم الإصدار

    // جالب لاتزامني: يُشغّل net::fetch() في خيط منفصل،
    // ويُبلِّغ الواجهة عند الانتهاء.
    AsyncFetcher m_async_fetcher;
};
