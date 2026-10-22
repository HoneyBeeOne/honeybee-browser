// =============================================================
// الملف: تنفيذ نافذة هَني بي
// الغرض: تنفيذ الصنف الرئيسي للنافذة.
//
// هذا الملف يحتوي على خمس دوال:
// المُنشئ: يبني الواجهة كاملة ويربط الإشارات.
// معالج ضغط الإدخال: عند ضغط مفتاح الإدخال في شريط العنوان.
// معالج الرجوع: عند الضغط على زر الرجوع.
// معالج التقدم: عند الضغط على زر التقدم.
// معالج إعادة التحميل: عند الضغط على زر إعادة التحميل.
// =============================================================

// استيراد إعلان الصنف نفسه.
#include "window.hpp"

// استيراد دالة الإصدار لعرض رقم الإصدار.
#include "base/version.hpp"

// استيراد دالة التحقق من الرابط.
#include "base/url.hpp"
#include "renderer/html/preview.hpp"

// =============================================================
// المُنشئ: بناء الواجهة
//
// المُنشئ مسؤول عن:
// ضبط خصائص النافذة، مثل العنوان والحجم.
// بناء كل عناصر الواجهة.
// ربط الإشارات بالمعالجات.
// تجميع العناصر في شجرة التخطيط.
//
// قائمة التهيئة ضرورية لأن بعض الأعضاء تحتاج معاملات
// في المُنشئ، مثل اتجاه الحاوية.
// =============================================================

HoneyBeeWindow::HoneyBeeWindow()
    : m_root_box(Gtk::Orientation::VERTICAL),
      m_toolbar_box(Gtk::Orientation::HORIZONTAL),
      m_status_bar(Gtk::Orientation::HORIZONTAL) {

    // ---------------------------------------------------------
    // الخطوة الأولى: ضبط خصائص النافذة
    // ---------------------------------------------------------
    set_title("HoneyBee Browser — متصفح");
    set_default_size(1024, 768);

    // ---------------------------------------------------------
    // الخطوة الثانية: إعداد أزرار التنقل
    //
    // كل زر له ثلاثة إعدادات:
    // التسمية، وهي رمز يمثل الوظيفة.
    // التلميح، وهو نص يظهر عند المرور بالماوس.
    // الإشارة، وهي الدالة التي تُستدعى عند الضغط.
    // ---------------------------------------------------------

    m_back_button.set_label("←");
    m_forward_button.set_label("→");
    m_reload_button.set_label("↻");

    m_back_button.set_tooltip_text("رجوع");
    m_forward_button.set_tooltip_text("تقدم");
    m_reload_button.set_tooltip_text("إعادة التحميل");

    // الأزرار الثلاثة مُفعَّلة الآن.
    // إعادة التحميل تعمل فعلياً.
    // أما الرجوع والتقدم، فيعرضان رسالة فقط، حتى
    // يُبنى سجل التنقل في مهمة لاحقة.
    m_back_button.set_sensitive(true);
    m_forward_button.set_sensitive(true);
    m_reload_button.set_sensitive(true);

    // ربط إشارة الضغط بمعالج كل زر.
    // الدالة الداخلية تلتقط هذا الكائن، للوصول إلى
    // الدالة العضو المناسبة.
    m_back_button.signal_clicked().connect(
        [this]() { on_back_clicked(); });

    m_forward_button.signal_clicked().connect(
        [this]() { on_forward_clicked(); });

    m_reload_button.signal_clicked().connect(
        [this]() { on_reload_clicked(); });

    // ---------------------------------------------------------
    // الخطوة الثالثة: إعداد شريط العنوان
    // ---------------------------------------------------------

    // نص مؤقت يظهر عندما يكون الحقل فارغاً.
    m_url_bar.set_placeholder_text("أدخل عنوان الموقع");

    // يمتد أفقياً ليملأ المساحة المتبقية من شريط الأدوات.
    m_url_bar.set_hexpand(true);

    // ربط إشارة التنشيط، التي تُطلَق عند ضغط مفتاح
    // الإدخال، بالمعالج الخاص بنا.
    m_url_bar.signal_activate().connect(
        [this]() { on_url_activated(); });

    // ---------------------------------------------------------
    // الخطوة الرابعة: تجميع شريط الأدوات
    //
    // نستخدم حاوية أفقية لتجميع العناصر بجانب بعضها:
    // زر الرجوع، ثم التقدم، ثم إعادة التحميل، ثم شريط
    // العنوان الذي يمتد ليملأ ما تبقى.
    // ---------------------------------------------------------

    m_toolbar_box.set_spacing(6);   // مسافة بين العناصر
    m_toolbar_box.set_margin(6);    // هامش خارجي

    m_toolbar_box.append(m_back_button);
    m_toolbar_box.append(m_forward_button);
    m_toolbar_box.append(m_reload_button);
    m_toolbar_box.append(m_url_bar);

    // ---------------------------------------------------------
    // الخطوة الخامسة: إعداد منطقة العرض
    //
    // في المرحلة الحالية، نستخدم نصاً لعرض محتوى
    // الصفحة. ولاحقاً، سيُستبدَل بمحرك عرض حقيقي.
    //
    // نبني النص الافتراضي بإضافة رقم الإصدار من الدالة،
    // وهذا يضمن وجود الإصدار في مكان واحد فقط.
    // ---------------------------------------------------------


    // ---------------------------------------------------------
    // الخطوة السادسة: إعداد شريط الحالة
    //
    // الشريط يتكون من عنصرين:
    // نص الحالة، وهو على اليسار.
    // رقم الإصدار، وهو على اليمين.
    // ---------------------------------------------------------

    m_status_label.set_text("جاهز");

    // نستخدم نفس دالة الإصدار، بادئة بحرف الفاء اللاتيني
    // الصغير للإشارة إلى الإصدار.
    m_version_label.set_text("v" + honeybee::version_string());

    // الامتداد الأفقي والمحاذاة إلى النهاية يدفعان
    // الإصدار إلى أقصى اليمين.
    m_version_label.set_hexpand(true);
    m_version_label.set_halign(Gtk::Align::END);

    m_status_bar.set_margin(6);
    m_status_bar.append(m_status_label);
    m_status_bar.append(m_version_label);

    // ---------------------------------------------------------
    // الخطوة السابعة: التجميع النهائي
    //
    // نُضيف العناصر الثلاثة إلى الحاوية الرئيسية من
    // الأعلى إلى الأسفل:
    // شريط الأدوات.
    // منطقة العرض.
    // شريط الحالة.
    // ---------------------------------------------------------

    m_root_box.append(m_toolbar_box);
    m_root_box.append(m_drawing_area);
    m_root_box.append(m_status_bar);

    // نُعيّن الحاوية الرئيسية كالابن الوحيد للنافذة.
    // View area setup: draw callback and welcome page.
    m_drawing_area.set_vexpand(true);
    m_drawing_area.set_draw_func(
        [this](const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
            on_draw(cr, w, h);
        });

    // ADR-018 Commit 4b: mouse motion drives :hover. GTK4
    // reports motion events in logical pixels, same units the
    // layout tree uses, so no scale-factor conversion needed.
    m_motion_controller = Gtk::EventControllerMotion::create();
    m_motion_controller->signal_motion().connect(
        [this](double x, double y) { on_mouse_motion(x, y); });
    m_motion_controller->signal_leave().connect(
        [this]() { on_mouse_leave(); });
    m_drawing_area.add_controller(m_motion_controller);

    const std::string welcome =
        "<html><head><title>HoneyBee Browser</title>"
        "<style>"
        "body { margin: 20px; }"
        "h1 { font-size: 32px; color: #d35400; }"
        "p { font-size: 16px; color: #333; padding: 4px; }"
        "</style></head><body>"
        "<h1>HoneyBee Browser</h1>"
        "<p>Version " + honeybee::version_string() + "</p>"
        "<p>Type a URL above and press Enter to load a page.</p>"
        "<p>هذا نص عربي. Rendered via Pango for correct shaping.</p>"
        "</body></html>";
    render_html(welcome);

    set_child(m_root_box);
}

// =============================================================
// معالج ضغط الإدخال: معالجة ضغط مفتاح الإدخال في شريط
// العنوان
//
// المنطق:
// نأخذ النص من شريط العنوان.
// نتحقق من صحته بدالة التحقق.
// فإن كان خاطئاً، نُحدّث شريط الحالة فوراً ونخرج.
// وإن كان صحيحاً:
// نُحدّث شريط الحالة برسالة جارٍ الجلب.
// نُعطّل حقل الإدخال، لمنع جلبتين متزامنتين.
// نبدأ الجلب في الخيط الخلفي.
// وفي رد النداء، الذي يُنفَّذ في الخيط الرئيسي:
// نُعيد تفعيل حقل الإدخال.
// فإن فشل الجلب، نعرض رسالة فشل.
// وإن نجح، نعرض أول خمس مئة حرف من المحتوى،
// ونحفظ الرابط في العضو المخصص لسجل التنقل.
// =============================================================

void HoneyBeeWindow::on_url_activated() {
    // الخطوة الأولى: أخذ النص من الحقل.
    const std::string url = m_url_bar.get_text();

    // الخطوة الثانية: التحقق من الرابط.
    if (!honeybee::is_valid_url(url)) {
        // رابط خاطئ. نُحدّث الشريط ونخرج. لا جلب.
        m_status_label.set_text("رابط غير صالح");
        return;
    }

    // الخطوة الثالثة: تجهيز الواجهة للجلب.
    m_status_label.set_text("جارٍ الجلب...");

    // تعطيل الحقل، لأن الجالب اللاتزامني لا يدعم
    // جلبتين متزامنتين.
    m_url_bar.set_sensitive(false);

    // الخطوة الرابعة: بدء الجلب في الخيط الخلفي.
    //
    // الدالة الداخلية تلتقط هذا الكائن، للوصول إلى
    // أعضاء النافذة. والمعامل يُمرَّر بالقيمة، لأنه
    // نتيجة جلب واحدة.
    //
    // ملاحظة مهمة: هذا الرد يُنفَّذ في الخيط الرئيسي،
    // بفضل الموزِّع داخل الجالب اللاتزامني. لذا تحديث
    // الواجهة هنا آمن تماماً.
    m_async_fetcher.fetch(url, [this, url](honeybee::net::Response r) {

        // إعادة تفعيل حقل الإدخال، لتمكين جلب جديد.
        m_url_bar.set_sensitive(true);

        // الخطوة الخامسة: معالجة نتيجة الجلب.

        // حالة الفشل: رمز الحالة ليس في نطاق النجاح.
        if (!r.success()) {
            m_status_label.set_text(
                "فشل الجلب، رمز الحالة: " +
                std::to_string(r.status_code));
            return;
        }

        // حالة النجاح: نحفظ الرابط في العضو المخصص،
        // لاستخدامه في إعادة التحميل لاحقاً.
        m_current_url = url;

        // ADR-018: mark this URL as visited and set it as the
        // base URL for the page about to be rendered. This
        // makes :visited match correctly during cascade, and
        // resolves the page's relative hrefs against the page
        // URL rather than against an empty base.
        m_pipeline.record_visit(url);

        // Run the full render pipeline (HTML -> layout -> paint).
        render_html(r.body);


        // تحديث شريط الحالة بحجم المحتوى الكامل.
        m_status_label.set_text(
            "تم الجلب، الحجم: " +
            std::to_string(r.body.size()) + " بايت");
    });
}

// =============================================================
// معالج الرجوع: عند الضغط على زر الرجوع
//
// في المرحلة الحالية، لا يوجد سجل تنقل حقيقي.
// لذا نعرض رسالة فقط في شريط الحالة.
// سجل التنقل الحقيقي مهمة لاحقة.
// =============================================================

void HoneyBeeWindow::on_back_clicked() {
    m_status_label.set_text("الرجوع غير مُفعَّل بعد، سيُضاف لاحقاً");
}

// =============================================================
// معالج التقدم: عند الضغط على زر التقدم
//
// في المرحلة الحالية، لا يوجد سجل تنقل حقيقي.
// لذا نعرض رسالة فقط في شريط الحالة.
// سجل التنقل الحقيقي مهمة لاحقة.
// =============================================================

void HoneyBeeWindow::on_forward_clicked() {
    m_status_label.set_text("التقدم غير مُفعَّل بعد، سيُضاف لاحقاً");
}

// =============================================================
// معالج إعادة التحميل: عند الضغط على زر إعادة التحميل
//
// المنطق:
// إن كان هناك رابط محفوظ، نضعه في شريط العنوان،
// ثم نعيد معالجة الرابط، فيبدأ الجلب من جديد.
// وإن لم يكن هناك رابط محفوظ، نعرض رسالة قصيرة.
// =============================================================

void HoneyBeeWindow::on_reload_clicked() {
    // التحقق من وجود رابط محفوظ.
    if (m_current_url.empty()) {
        m_status_label.set_text("لا يوجد رابط لإعادة تحميله");
        return;
    }

    // نضع الرابط في شريط العنوان، ليراه المستخدم.
    m_url_bar.set_text(m_current_url);

    // نعيد معالجة الرابط، فيبدأ الجلب من جديد.
    on_url_activated();
}

// =============================================================
// render_html: run the full pipeline and repaint.
// Called on successful fetch and at startup.
// =============================================================

void HoneyBeeWindow::render_html(const std::string& html) {
    m_pipeline.render(html, static_cast<float>(m_drawing_area.get_width()));

    const std::string title = m_pipeline.title();
    if (!title.empty()) {
        set_title(title + " — HoneyBee");
    }
    m_drawing_area.queue_draw();
}

// =============================================================
// on_draw: paint the layout tree on the DrawingArea.
// Relayouts if the viewport width has changed since the last draw.
// =============================================================

void HoneyBeeWindow::on_draw(const Cairo::RefPtr<Cairo::Context>& cr,
                             int width, int height) {
    (void)height;

    if (width != m_last_width) {
        m_last_width = width;
        m_pipeline.relayout(static_cast<float>(width));
    }

    // White background.
    cr->set_source_rgb(1.0, 1.0, 1.0);
    cr->paint();

    // Paint the layout tree.
    m_pipeline.paint(cr->cobj());
}

// =============================================================
// ADR-018 Commit 4b: mouse motion handlers.
//
// On every motion event, hit-test the cursor position, compare
// against the current hovered_element, and if it changed:
// update the pipeline's RenderState, re-run cascade + layout
// (v1 cost — a restyle-only path is a future optimization),
// and queue a repaint.
//
// on_mouse_leave fires when the cursor exits the drawing area.
// =============================================================

void HoneyBeeWindow::on_mouse_motion(double x, double y) {
    if (!m_pipeline.has_page()) return;

    const auto* el = m_pipeline.hit_test(
        static_cast<float>(x), static_cast<float>(y));

    // No change: skip relayout (avoids churn on intra-element motion).
    if (el == m_pipeline.state().hovered_element) return;

    m_pipeline.set_hovered_element(el);

    if (m_last_width > 0) {
        m_pipeline.relayout(static_cast<float>(m_last_width));
    }
    m_drawing_area.queue_draw();
}

void HoneyBeeWindow::on_mouse_leave() {
    if (!m_pipeline.has_page()) return;
    if (m_pipeline.state().hovered_element == nullptr) return;

    m_pipeline.set_hovered_element(nullptr);
    if (m_last_width > 0) {
        m_pipeline.relayout(static_cast<float>(m_last_width));
    }
    m_drawing_area.queue_draw();
}
