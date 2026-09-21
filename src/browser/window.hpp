#pragma once

#include <gtkmm.h>

class HoneyBeeWindow : public Gtk::Window {
public:
    HoneyBeeWindow();

private:
    Gtk::Box m_root_box;    // الحاوية الرئيسية: تخطيط عمودي
    Gtk::Entry m_url_bar;   // شريط العنوان
    Gtk::Label m_label;     // النص المؤقت في وسط النافذة
};
