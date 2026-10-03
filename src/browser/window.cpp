// =============================================================
// الملف: تنفيذ نافذة هَني بي
// الغرض: تنفيذ الصنف الرئيسي للنافذة.
// =============================================================

// استيراد إعلان الصنف نفسه.
#include "window.hpp"

// استيراد دالة الإصدار.
#include "base/version.hpp"

// استيراد دالة التحقق من الرابط.
#include "base/url.hpp"

// استيراد محلل إتش تي إم إل.
#include "renderer/html/parser.hpp"

// استيراد العارض.
#include "renderer/renderer.hpp"

// =============================================================
// المُنشئ: بناء الواجهة
// =============================================================

HoneyBeeWindow::HoneyBeeWindow()
    : m_root_box(Gtk::Orientation::VERTICAL),
      m_toolbar_box(Gtk::Orientation::HORIZONTAL),
      m_status_bar(Gtk::Orientation::HORIZONTAL) {

    set_title("HoneyBee Browser — متصفح");
    set_default_size(1024, 768);

    // ---------------------------------------------------------
    // إعداد أزرار التنقل
    // ---------------------------------------------------------
    m_back_button.set_label("←");
    m_forward_button.set_label("→");
    m_reload_button.set_label("↻");

    m_back_button.set_tooltip_text("رجوع");
    m_forward_button.set_tooltip_text("تقدم");
    m_reload_button.set_tooltip_text("إعادة التحميل");

    m_back_button.set_sensitive(true);
    m_forward_button.set_sensitive(true);
    m_reload_button.set_sensitive(true);

    m_back_button.signal_clicked().connect(
        [this]() { on_back_clicked(); });
    m_forward_button.signal_clicked().connect(
        [this]() { on_forward_clicked(); });
    m_reload_button.signal_clicked().connect(
        [this]() { on_reload_clicked(); });

    // ---------------------------------------------------------
    // إعداد شريط العنوان
    // ---------------------------------------------------------
    m_url_bar.set_placeholder_text("أدخل عنوان الموقع");
    m_url_bar.set_hexpand(true);

    m_url_bar.signal_activate().connect(
        [this]() { on_url_activated(); });

    // ---------------------------------------------------------
    // تجميع شريط الأدوات
    // ---------------------------------------------------------
    m_toolbar_box.set_spacing(6);
    m_toolbar_box.set_margin(6);

    m_toolbar_box.append(m_back_button);
    m_toolbar_box.append(m_forward_button);
    m_toolbar_box.append(m_reload_button);
    m_toolbar_box.append(m_url_bar);

    // ---------------------------------------------------------
    // إعداد منطقة المحتوى
    //
    // نستخدم منطقة قابلة للتمرير. في البداية، تحتوي على
    // النص المؤقت. وبعد أول جلب ناجح، يُستبدَل بمحتوى
    // الصفحة المُحوَّل.
    // ---------------------------------------------------------
    m_content_area.set_policy(Gtk::PolicyType::AUTOMATIC,
                              Gtk::PolicyType::AUTOMATIC);
    m_content_area.set_vexpand(true);
    m_content_area.set_hexpand(true);

    const std::string label_text =
        "HoneyBee Browser\n\nالمرحلة صفر — الإصدار " +
        honeybee::version_string() +
        "\n\nسيُعرض محتوى الصفحة هنا";
    m_label.set_text(label_text);
    m_label.set_justify(Gtk::Justification::CENTER);
    m_label.set_margin(24);
    m_content_area.set_child(m_label);

    // ---------------------------------------------------------
    // إعداد شريط الحالة
    // ---------------------------------------------------------
    m_status_label.set_text("جاهز");
    m_version_label.set_text("v" + honeybee::version_string());
    m_version_label.set_hexpand(true);
    m_version_label.set_halign(Gtk::Align::END);

    m_status_bar.set_margin(6);
    m_status_bar.append(m_status_label);
    m_status_bar.append(m_version_label);

    // ---------------------------------------------------------
    // التجميع النهائي
    // ---------------------------------------------------------
    m_root_box.append(m_toolbar_box);
    m_root_box.append(m_content_area);
    m_root_box.append(m_status_bar);

    set_child(m_root_box);
}

// =============================================================
// معالج ضغط الإدخال
//
// عند ضغط مفتاح الإدخال:
// نتحقق من الرابط.
// فإن كان صحيحاً، نجلبه في الخيط الخلفي.
// وعند وصول المحتوى، نحوّله إلى عناصر واجهة.
// =============================================================

void HoneyBeeWindow::on_url_activated() {
    const std::string url = m_url_bar.get_text();

    if (!honeybee::is_valid_url(url)) {
        m_status_label.set_text("رابط غير صالح");
        return;
    }

    m_status_label.set_text("جارٍ الجلب...");
    m_url_bar.set_sensitive(false);

    m_async_fetcher.fetch(url, [this, url](honeybee::net::Response r) {
        m_url_bar.set_sensitive(true);

        if (!r.success()) {
            m_status_label.set_text(
                "فشل الجلب، رمز الحالة: " +
                std::to_string(r.status_code));
            return;
        }

        m_current_url = url;

        // تحليل المحتوى وتحويله إلى عناصر واجهة.
        const auto parsed = honeybee::html::parse(r.body);
        if (!parsed.success) {
            m_status_label.set_text("فشل تحليل إتش تي إم إل");
            return;
        }

        // استبدال محتوى المنطقة بالنتيجة.
        auto* rendered =
            honeybee::renderer::Renderer::render(parsed.root);
        if (rendered != nullptr) {
            m_content_area.set_child(*rendered);
        }

        m_status_label.set_text(
            "تم الجلب، الحجم: " +
            std::to_string(r.body.size()) + " بايت");
    });
}

// =============================================================
// معالج الرجوع
// =============================================================

void HoneyBeeWindow::on_back_clicked() {
    m_status_label.set_text("الرجوع غير مُفعَّل بعد، سيُضاف لاحقاً");
}

// =============================================================
// معالج التقدم
// =============================================================

void HoneyBeeWindow::on_forward_clicked() {
    m_status_label.set_text("التقدم غير مُفعَّل بعد، سيُضاف لاحقاً");
}

// =============================================================
// معالج إعادة التحميل
// =============================================================

void HoneyBeeWindow::on_reload_clicked() {
    if (m_current_url.empty()) {
        m_status_label.set_text("لا يوجد رابط لإعادة تحميله");
        return;
    }

    m_url_bar.set_text(m_current_url);
    on_url_activated();
}
