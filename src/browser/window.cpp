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

#include <cmath>

// استيراد دالة الإصدار لعرض رقم الإصدار.
#include "base/version.hpp"

// استيراد دالة التحقق من الرابط.
#include "base/url.hpp"
#include "renderer/html/preview.hpp"

namespace dom = honeybee::renderer::dom;
using honeybee::browser::NavKind;

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
    // ADR-019: Back/Forward start disabled -- history empty.
    // Reload stays enabled but shows a status message if no URL
    // has been loaded yet.
    m_back_button.set_sensitive(false);
    m_forward_button.set_sensitive(false);
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

    // ADR-021: wrap the drawing area in a ScrolledWindow so
    // content taller than the viewport is scrollable. The
    // DrawingArea's size request grows to the full layout
    // height (set by sync_drawing_area_size after every
    // relayout); ScrolledWindow then provides vertical scroll.
    // Horizontal scroll is disabled -- v1 lays out at viewport
    // width, no horizontal overflow.
    // Pointer coordinates remain in layout space: GDK
    // translates via the widget hierarchy, so hit-test and
    // hover from ADR-018/019/020 need no changes.
    m_scroll_window.set_child(m_drawing_area);
    m_scroll_window.set_policy(Gtk::PolicyType::NEVER,
                               Gtk::PolicyType::AUTOMATIC);
    // ADR-021: non-overlay scrollbar takes real space on the
    // right and is visibly present when content overflows.
    // Overlay scrollbars are ~4px semi-transparent and auto-hide
    // between gestures -- easy to miss, and harder to drag.
    m_scroll_window.set_overlay_scrolling(false);
    m_scroll_window.set_vexpand(true);

    m_root_box.append(m_scroll_window);

    m_root_box.append(m_status_bar);

    // نُعيّن الحاوية الرئيسية كالابن الوحيد للنافذة.
    // View area setup: draw callback and welcome page.
    m_drawing_area.set_hexpand(true);
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

    // ADR-018 Commit 5: click navigation. A GestureClick on the
    // drawing area navigates when the release point is over an
    // <a href>. n_press == 1 skips the second release of a
    // double-click (which fires n_press == 2).
    m_click_controller = Gtk::GestureClick::create();
    m_click_controller->signal_released().connect(
        [this](int n_press, double x, double y) {
            if (n_press == 1) on_link_clicked(x, y);
        });
    m_drawing_area.add_controller(m_click_controller);


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
    // Delegate to navigate_to so the fetch path is shared with
    // on_link_clicked (Commit 5, ADR-018).
    navigate_to(m_url_bar.get_text(), NavKind::New);
}

void HoneyBeeWindow::navigate_to(const std::string& url, NavKind kind) {

    // الخطوة الثانية: التحقق من الرابط.
    if (!honeybee::is_valid_url(url)) {
        // رابط خاطئ. نُحدّث الشريط ونخرج. لا جلب.
        m_status_label.set_text("رابط غير صالح");
        return;
    }

    // ADR-020: cache short-circuit for Back/Forward. On a hit,
    // we bypass the fetch entirely -- no "جارٍ الجلب..." state,
    // no button disable. Back feels instant because it is.
    // New and Reload always fetch (see the ADR: without HTTP
    // cache headers, serving stale content on an explicit
    // navigation would be surprising).
    if (kind == NavKind::Back || kind == NavKind::Forward) {
        if (auto cached = m_cache.get(url)) {
            m_current_url = url;
            m_url_bar.set_text(url);
            // record_visit must still fire: RenderState::base_url
            // is used for relative-href resolution during cascade,
            // and visited_urls re-insertion is idempotent.
            m_pipeline.record_visit(url);
            update_nav_buttons();
            render_html(*cached);
            // Distinct message from a real fetch -- on localhost
            // a hit and a fetch are both instantaneous, so the
            // status text is the only reliable visual signal
            // that the cache was consulted.
            m_status_label.set_text(
                "من الذاكرة، الحجم: " +
                std::to_string(cached->size()) + " بايت");
            return;
        }
    }

    // الخطوة الثالثة: تجهيز الواجهة للجلب.
    // When called from on_link_clicked, the URL bar hasn't been
    // updated -- show the destination. When called from
    // on_url_activated, this is a no-op (same text).
    m_url_bar.set_text(url);
    m_status_label.set_text("جارٍ الجلب...");

    // تعطيل الحقل، لأن الجالب اللاتزامني لا يدعم
    // جلبتين متزامنتين.
    m_url_bar.set_sensitive(false);

    // ADR-019: disable nav buttons during in-flight fetch to
    // prevent racing with the async navigation. update_nav_buttons()
    // restores them in the fetch callback.
    m_back_button.set_sensitive(false);
    m_forward_button.set_sensitive(false);

    // الخطوة الرابعة: بدء الجلب في الخيط الخلفي.
    //
    // الدالة الداخلية تلتقط هذا الكائن، للوصول إلى
    // أعضاء النافذة. والمعامل يُمرَّر بالقيمة، لأنه
    // نتيجة جلب واحدة.
    //
    // ملاحظة مهمة: هذا الرد يُنفَّذ في الخيط الرئيسي،
    // بفضل الموزِّع داخل الجالب اللاتزامني. لذا تحديث
    // الواجهة هنا آمن تماماً.
    m_async_fetcher.fetch(url, [this, url, kind](honeybee::net::Response r) {

        // إعادة تفعيل حقل الإدخال، لتمكين جلب جديد.
        m_url_bar.set_sensitive(true);

        // الخطوة الخامسة: معالجة نتيجة الجلب.

        // حالة الفشل: رمز الحالة ليس في نطاق النجاح.
        if (!r.success()) {
            // ADR-019: on Back/Forward failure, roll the cursor
            // back so the user isn't stranded on a page they
            // didn't navigate to. New failures don't touch
            // history (push is deferred to success). Reload
            // never touched history in the first place.
            if (kind == NavKind::Back)    (void)m_history.forward();
            if (kind == NavKind::Forward) (void)m_history.back();

            // Restore the URL bar to the current (unchanged)
            // URL -- the user is still on the previous page.
            if (!m_current_url.empty()) {
                m_url_bar.set_text(m_current_url);
            }
            m_status_label.set_text(
                "فشل الجلب، رمز الحالة: " +
                std::to_string(r.status_code));
            update_nav_buttons();
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

        // ADR-020: cache the freshly-fetched body. Runs for
        // every success -- New (pushes history + caches), Back
        // (miss, defensive), Forward (miss, defensive), Reload
        // (overwrites the previous entry with the fresh body).
        m_cache.put(url, r.body);

        // ADR-019: history bookkeeping. New pushes; Back/Forward
        // cursor was already moved by the caller; Reload doesn't
        // touch history. update_nav_buttons() runs after to
        // reflect the new can_back/can_forward state.
        if (kind == NavKind::New) {
            m_history.push(url);
        }
        update_nav_buttons();

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
    auto target = m_history.back();
    if (!target) return;  // button should be disabled if !can_back
    navigate_to(*target, NavKind::Back);
}

// =============================================================
// معالج التقدم: عند الضغط على زر التقدم
//
// في المرحلة الحالية، لا يوجد سجل تنقل حقيقي.
// لذا نعرض رسالة فقط في شريط الحالة.
// سجل التنقل الحقيقي مهمة لاحقة.
// =============================================================

void HoneyBeeWindow::on_forward_clicked() {
    auto target = m_history.forward();
    if (!target) return;  // button should be disabled if !can_forward
    navigate_to(*target, NavKind::Forward);
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
    if (m_current_url.empty()) {
        m_status_label.set_text("لا يوجد رابط لإعادة تحميله");
        return;
    }
    // ADR-019: NavKind::Reload so history is untouched -- pressing
    // Reload N times must not create N history entries.
    navigate_to(m_current_url, NavKind::Reload);
}

void HoneyBeeWindow::update_nav_buttons() {
    m_back_button.set_sensitive(m_history.can_back());
    m_forward_button.set_sensitive(m_history.can_forward());
}

// =============================================================
// render_html: run the full pipeline and repaint.
// Called on successful fetch and at startup.
// =============================================================

void HoneyBeeWindow::render_html(const std::string& html) {
    m_pipeline.render(html, static_cast<float>(m_drawing_area.get_width()));
    sync_drawing_area_size();

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
                             int width, int /*height*/) {
    // ADR-021: width is the DrawingArea's allocated width (which
    // shrinks if a vertical scrollbar appears); height is the
    // full DrawingArea height (= layout height), not the
    // viewport. We paint the entire layout tree; Cairo clips to
    // the visible viewport region automatically.
    if (width != m_last_width) {
        m_last_width = width;
        m_pipeline.relayout(static_cast<float>(width));
        sync_drawing_area_size();
    }

    // White background.
    cr->set_source_rgb(1.0, 1.0, 1.0);
    cr->paint();

    // Paint the layout tree.
    m_pipeline.paint(cr->cobj());
}

void HoneyBeeWindow::sync_drawing_area_size() {
    if (!m_pipeline.has_page()) return;
    if (m_last_width <= 0) return;
    const float h = m_pipeline.layout_height();
    if (h <= 0.0f) return;
    const int h_px = static_cast<int>(std::ceil(h));
    m_drawing_area.set_size_request(-1, h_px);
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

// =============================================================
// ADR-018 Commit 5: link-click navigation.
//
// Hit-tests the click, walks up the DOM to find the nearest
// <a> ancestor (hit_test may return <strong>/<em> inside the
// <a>), extracts href, resolves it against the current page's
// base URL, and navigates via navigate_to.
// =============================================================

void HoneyBeeWindow::on_link_clicked(double x, double y) {
    if (!m_pipeline.has_page()) return;

    const auto* el = m_pipeline.hit_test(
        static_cast<float>(x), static_cast<float>(y));
    if (!el) return;

    const dom::Element* a = el;
    while (a && a->tag() != "a") {
        const dom::Node* p = a->parent();
        a = (p && p->type() == dom::NodeType::Element)
            ? static_cast<const dom::Element*>(p)
            : nullptr;
    }
    if (!a) return;

    const std::string href = a->get_attribute("href");
    if (href.empty()) return;

    const std::string resolved =
        honeybee::resolve_url(href, m_pipeline.state().base_url);
    if (!honeybee::is_valid_url(resolved)) {
        m_status_label.set_text("رابط غير صالح");
        return;
    }

    navigate_to(resolved, NavKind::New);
}
