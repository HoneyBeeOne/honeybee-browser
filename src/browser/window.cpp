#include "window.hpp"
#include "base/version.hpp"

HoneyBeeWindow::HoneyBeeWindow()
    : m_root_box(Gtk::Orientation::VERTICAL),
      m_toolbar_box(Gtk::Orientation::HORIZONTAL),
      m_status_bar(Gtk::Orientation::HORIZONTAL) {
    set_title("HoneyBee Browser — متصفح");
    set_default_size(1024, 768);

    // إعداد أزرار التنقل: رجوع، تقدم، إعادة التحميل
    m_back_button.set_label("←");
    m_forward_button.set_label("→");
    m_reload_button.set_label("↻");

    // تلميحات توضيحية تظهر عند المرور بالماوس
    m_back_button.set_tooltip_text("رجوع");
    m_forward_button.set_tooltip_text("تقدم");
    m_reload_button.set_tooltip_text("إعادة التحميل");

    // تعطيل الأزرار مؤقتاً لأن التنقل لم يُفعَّل بعد
    m_back_button.set_sensitive(false);
    m_forward_button.set_sensitive(false);
    m_reload_button.set_sensitive(false);

    // شريط العنوان: حقل نص أحادي السطر
    m_url_bar.set_placeholder_text("أدخل عنوان الموقع");
    m_url_bar.set_hexpand(true);    // يمتد أفقياً ليملأ المساحة المتبقية

    // تجميع الأزرار وشريط العنوان في صف أفقي واحد
    m_toolbar_box.set_spacing(6);   // مسافة بين العناصر
    m_toolbar_box.set_margin(6);    // هامش خارجي حول الشريط
    m_toolbar_box.append(m_back_button);
    m_toolbar_box.append(m_forward_button);
    m_toolbar_box.append(m_reload_button);
    m_toolbar_box.append(m_url_bar);

    // النص المؤقت أسفل شريط الأدوات
    // نستخدم نسخة الإصدار بدلاً من كتابتها مباشرة، حتى تكون في مكان واحد
    const std::string label_text =
        "HoneyBee Browser\n\nالمرحلة صفر — الإصدار " +
        honeybee::version_string() +
        "\n\nسيُعرض محتوى الصفحة هنا";
    m_label.set_text(label_text);
    m_label.set_justify(Gtk::Justification::CENTER);
    m_label.set_margin(24);
    m_label.set_vexpand(true);      // يمتد رأسياً ليملأ المساحة المتبقية

    // إعداد شريط الحالة في الأسفل
    m_status_label.set_text("جاهز");
    m_version_label.set_text("v" + honeybee::version_string());
    m_version_label.set_hexpand(true);
    m_version_label.set_halign(Gtk::Align::END);

    m_status_bar.set_margin(6);
    m_status_bar.append(m_status_label);
    m_status_bar.append(m_version_label);

    // التخطيط العمودي: شريط الأدوات في الأعلى، النص في الوسط، وشريط الحالة في الأسفل
    m_root_box.append(m_toolbar_box);
    m_root_box.append(m_label);
    m_root_box.append(m_status_bar);

    set_child(m_root_box);
}
