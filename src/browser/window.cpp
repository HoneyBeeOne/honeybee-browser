#include "window.hpp"

HoneyBeeWindow::HoneyBeeWindow()
    : m_root_box(Gtk::Orientation::VERTICAL) {
    set_title("HoneyBee Browser — متصفح");
    set_default_size(1024, 768);

    // شريط العنوان: حقل نص أحادي السطر في أعلى النافذة
    // النص المؤقت بالعربية لأن الجمهور المستهدف ناطق بالعربية
    m_url_bar.set_placeholder_text("أدخل عنوان الموقع");
    m_url_bar.set_hexpand(true);    // يمتد أفقياً ليملأ المساحة المتاحة
    m_url_bar.set_margin(6);        // هامش خارجي حول الشريط

    // النص المؤقت أسفل شريط العنوان
    m_label.set_text("HoneyBee Browser\n\nالمرحلة صفر — الإصدار 0.1.0\n\nسيُعرض محتوى الصفحة هنا");
    m_label.set_justify(Gtk::Justification::CENTER);
    m_label.set_margin(24);
    m_label.set_vexpand(true);      // يمتد رأسياً ليملأ المساحة المتبقية

    // التخطيط العمودي: شريط العنوان في الأعلى، والنص تحته
    m_root_box.append(m_url_bar);
    m_root_box.append(m_label);

    set_child(m_root_box);
}
