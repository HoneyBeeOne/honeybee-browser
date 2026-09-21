#pragma once

#include <gtkmm.h>

class HoneyBeeWindow : public Gtk::Window {
public:
    HoneyBeeWindow();

private:
    Gtk::Box m_root_box;            // الحاوية الرئيسية: تخطيط عمودي
    Gtk::Box m_toolbar_box;         // شريط الأدوات: تخطيط أفقي
    Gtk::Button m_back_button;      // زر الرجوع
    Gtk::Button m_forward_button;   // زر التقدم
    Gtk::Button m_reload_button;    // زر إعادة التحميل
    Gtk::Entry m_url_bar;           // شريط العنوان
    Gtk::Label m_label;             // النص المؤقت في وسط النافذة
};
