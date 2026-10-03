// =============================================================
// الملف: تنفيذ العارض
// الغرض: تنفيذ صنف العارض الذي يحوّل شجرة العقد إلى عناصر
// واجهة جي تي كي.
//
// كيف يعمل؟
// نمر على كل عقدة في الشجرة.
// إن كانت عقدة نص، نُنتج نصاً عادياً.
// وإن كانت عقدة عنصر، نُنتج عنصر واجهة مناسباً لاسم الوسم.
// ونُضيف الأبناء إلى الأب، فنبني الشجرة المرئية تدريجياً.
//
// إدارة الذاكرة:
// نستخدم دالة الإدارة في جي تي كي لكل عنصر، فتصبح
// ملكيتها للأب الذي يضمّها. وعند تحرير الأب، تُحرَّر
// كل الأبناء تلقائياً. هذا يمنع تسرّب الذاكرة.
// =============================================================

// استيراد إعلان العارض.
#include "renderer/renderer.hpp"

// استيراد أدوات النص والعلامات:
// نص، لبناء النصوص.
// علامات، لتحويل النص إلى صيغة آمنة.
#include <string>

#include <glibmm/markup.h>

namespace honeybee::renderer {

namespace {

// -------------------------------------------------------------
// إعداد الأنماط البصرية
//
// نُعرّف الأنماط مرة واحدة عند أول استدعاء للعارض.
// والأنماط تشمل: حجم الخط لكل مستوى عنوان، وهامش الفقرات.
//
// لماذا نستخدم الأنماط بدل ضبط كل عنصر يدوياً؟
// لتوحيد الشكل، ولسهولة التعديل لاحقاً.
// -------------------------------------------------------------
void ensure_css() {
    // هل سبق إعداد الأنماط؟
    static bool loaded = false;
    if (loaded) {
        return;
    }

    // إنشاء موفر الأنماط.
    auto provider = Gtk::CssProvider::create();

    // تحميل الأنماط.
    // الأصناف تبدأ بالبادئة hb اختصاراً لاسم المشروع.
    provider->load_from_data(
        ".hb-h1 { font-size: 24pt; font-weight: bold; margin: 12px 0 6px 0; }\n"
        ".hb-h2 { font-size: 20pt; font-weight: bold; margin: 10px 0 5px 0; }\n"
        ".hb-h3 { font-size: 16pt; font-weight: bold; margin: 8px 0 4px 0; }\n"
        ".hb-h4 { font-size: 14pt; font-weight: bold; margin: 6px 0 3px 0; }\n"
        ".hb-h5 { font-size: 12pt; font-weight: bold; margin: 4px 0 2px 0; }\n"
        ".hb-h6 { font-size: 11pt; font-weight: bold; margin: 4px 0 2px 0; }\n"
    );

    // إضافة الموفر إلى عرض الشاشة.
    // الأولوية: التطبيق، لتجاوز الأنماط الافتراضية.
    Gtk::StyleContext::add_provider_for_display(
        Gdk::Display::get_default(),
        provider,
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    loaded = true;
}

// -------------------------------------------------------------
// بناء علامات من شجرة عقد
//
// نحول شجرة العقد إلى نص بصيغة العلامات التي يفهمها
// عنصر النص في جي تي كي. والعلامات تدعم: العريض، المائل،
// الروابط، الفواصل.
//
// مثال:
// عقدة عنصر من نوع التوكيد، داخلها نص.
// تُحوَّل إلى علامة العريض مع النص.
// -------------------------------------------------------------
std::string build_markup(const html::NodePtr& node) {
    if (!node) {
        return "";
    }

    // عقدة نص: نُعيد النص مع تهريبه.
    // التهريب يحوّل الرموز الخاصة إلى صيغة آمنة.
    if (node->is_text()) {
        return Glib::Markup::escape_text(node->text);
    }

    // عقدة عنصر: نبني علامات الأبناء أولاً.
    std::string inner;
    for (const auto& child : node->children) {
        inner += build_markup(child);
    }

    // ثم نُطبّق العلامة المناسبة حسب اسم الوسم.
    const std::string& tag = node->tag;
    if (tag == "b" || tag == "strong") {
        return "<b>" + inner + "</b>";
    }
    if (tag == "em" || tag == "i") {
        return "<i>" + inner + "</i>";
    }
    if (tag == "br") {
        return "\n";
    }
    return inner;
}

// -------------------------------------------------------------
// جمع النصوص من شجرة عقد
//
// نستخدم هذا في الروابط، حيث نحتاج نص الرابط كنص عادي،
// لا كعلامات.
// -------------------------------------------------------------
std::string collect_text(const html::NodePtr& node) {
    if (!node) {
        return "";
    }
    if (node->is_text()) {
        return node->text;
    }
    std::string result;
    for (const auto& child : node->children) {
        result += collect_text(child);
    }
    return result;
}

// -------------------------------------------------------------
// إنشاء عنصر نصي جاهز
//
// نُعدّ عنصر النص بخصائص موحّدة:
// محاذاة إلى البداية، لفّ النص عند الحاجة، صنف نمط.
// -------------------------------------------------------------
Gtk::Label* make_label(const std::string& content,
                       const std::string& css_class,
                       bool is_markup = false) {
    auto label = Gtk::manage(new Gtk::Label());

    if (is_markup) {
        try {
            label->set_markup(content);
        } catch (...) {
            // في حال فشل تحليل العلامات، نُعيد النص كما هو.
            label->set_text(content);
        }
    } else {
        label->set_text(content);
    }

    // محاذاة إلى البداية، ولفّ النص، ومحاذاة سطرية إلى اليسار.
    label->set_halign(Gtk::Align::START);
    label->set_wrap(true);
    label->set_xalign(0.0f);

    if (!css_class.empty()) {
        label->get_style_context()->add_class(css_class);
    }

    return label;
}

}  // النهاية

namespace {

// -------------------------------------------------------------
// تحويل عقدة نص إلى عنصر نصي
// -------------------------------------------------------------
Gtk::Widget* make_text_widget(const html::NodePtr& node) {
    return make_label(node->text, "");
}

// -------------------------------------------------------------
// تحويل قائمة إلى عنصر واجهة
//
// الوسم الممرر يحدد نوع القائمة: نقطية أو مرقمة.
// نمر على أبناء القائمة من نوع العنصر، ونُنتج لكل
// عنصر نصاً مع نقطة أو رقم.
// -------------------------------------------------------------
Gtk::Widget* make_list(const html::NodePtr& node, bool ordered) {
    auto box = Gtk::manage(new Gtk::Box(Gtk::Orientation::VERTICAL));
    box->set_spacing(4);
    box->set_margin_start(16);

    int counter = 1;
    for (const auto& child : node->children) {
        // نتجاهل أي عقدة ليست عنصر قائمة.
        if (!child->is_element() || child->tag != "li") {
            continue;
        }

        // نبني نص العنصر مع العلامات.
        std::string inner;
        for (const auto& sub : child->children) {
            inner += build_markup(sub);
        }

        // نُضيف النقطة أو الرقم في البداية.
        std::string prefix = ordered
            ? std::to_string(counter) + ". "
            : "\u2022 ";  // نقطة عربية.

        auto label = make_label(prefix + inner, "", true);
        box->append(*label);
        ++counter;
    }

    return box;
}

// -------------------------------------------------------------
// تحويل عنصر عنوان إلى عنصر نصي
//
// الوسم الممرر يحدد مستوى العنوان، وبالتالي حجم الخط.
// -------------------------------------------------------------
Gtk::Widget* make_heading(const html::NodePtr& node,
                          const std::string& css_class) {
    // نبني محتوى العنوان مع العلامات.
    std::string content;
    for (const auto& child : node->children) {
        content += build_markup(child);
    }

    return make_label(content, css_class, true);
}

// -------------------------------------------------------------
// تحويل عنصر رابط إلى زر رابط
//
// الرابط يُعرض كزر قابل للنقر. وسمة الرابط تحدد الوجهة.
// إن لم تكن السمة موجودة، نعرض النص فقط.
// -------------------------------------------------------------
Gtk::Widget* make_link(const html::NodePtr& node) {
    // الحصول على وجهة الرابط.
    std::string href;
    const auto it = node->attributes.find("href");
    if (it != node->attributes.end()) {
        href = it->second;
    }

    // جمع نص الرابط.
    std::string text = collect_text(node);
    if (text.empty()) {
        text = href;
    }

    // إن لم توجد وجهة، نعرض النص فقط.
    if (href.empty()) {
        return make_label(text, "");
    }

    // إنشاء زر رابط.
    auto link = Gtk::manage(new Gtk::LinkButton(href, text));
    link->set_halign(Gtk::Align::START);
    return link;
}

}  // النهاية

// -------------------------------------------------------------
// تحويل عقدة عنصر إلى عنصر واجهة
//
// نستخدم اسم الوسم لتحديد نوع العنصر الناتج.
// الوسوم غير المعروفة تُعامَل كحاويات شفافة.
// -------------------------------------------------------------
Gtk::Widget* Renderer::render_element(const html::NodePtr& node) {
    const std::string& tag = node->tag;

    // العناوين.
    if (tag == "h1") {
        return make_heading(node, "hb-h1");
    }
    if (tag == "h2") {
        return make_heading(node, "hb-h2");
    }
    if (tag == "h3") {
        return make_heading(node, "hb-h3");
    }
    if (tag == "h4") {
        return make_heading(node, "hb-h4");
    }
    if (tag == "h5") {
        return make_heading(node, "hb-h5");
    }
    if (tag == "h6") {
        return make_heading(node, "hb-h6");
    }

    // الفقرة.
    if (tag == "p") {
        std::string content;
        for (const auto& child : node->children) {
            content += build_markup(child);
        }
        return make_label(content, "", true);
    }

    // الرابط.
    if (tag == "a") {
        return make_link(node);
    }

    // القوائم.
    if (tag == "ul") {
        return make_list(node, false);
    }
    if (tag == "ol") {
        return make_list(node, true);
    }

    // الفاصل.
    if (tag == "br") {
        return make_label("", "");
    }

    // الوسوم المضمّنة للنص.
    // التوكيد، التمييز، التمييز البسيط: نُعيد النص فقط،
    // لأن التنسيق يُطبّق عبر العلامات في الفقرة الأم.
    if (tag == "b" || tag == "strong" ||
        tag == "em" || tag == "i" ||
        tag == "span") {
        std::string content;
        for (const auto& child : node->children) {
            content += build_markup(child);
        }
        return make_label(content, "", true);
    }

    // الوسوم غير المدعومة، والحاويات.
    // نُعيد حاوية عمودية تحتوي على كل الأبناء.
    return render_children(node);
}

// -------------------------------------------------------------
// تحويل عقدة نص إلى عنصر واجهة
// -------------------------------------------------------------
Gtk::Widget* Renderer::render_text(const html::NodePtr& node) {
    return make_text_widget(node);
}

// -------------------------------------------------------------
// تحويل عقدة واحدة إلى عنصر واجهة
// -------------------------------------------------------------
Gtk::Widget* Renderer::render_node(const html::NodePtr& node) {
    if (!node) {
        return nullptr;
    }
    if (node->is_text()) {
        return render_text(node);
    }
    return render_element(node);
}

// -------------------------------------------------------------
// تحويل أبناء عقدة إلى حاوية عمودية
//
// نستدعيها من العناصر التي لا تحتاج تنسيقاً خاصاً،
// ومن الجذر. والنتيجة حاوية عمودية تحتوي على كل الأبناء.
// -------------------------------------------------------------
Gtk::Widget* Renderer::render_children(const html::NodePtr& node) {
    auto box = Gtk::manage(new Gtk::Box(Gtk::Orientation::VERTICAL));
    box->set_spacing(6);
    box->set_margin(12);

    for (const auto& child : node->children) {
        auto* widget = render_node(child);
        if (widget) {
            box->append(*widget);
        }
    }

    return box;
}

// -------------------------------------------------------------
// الدالة العامة: تحويل الشجرة إلى عنصر واجهة واحد
// -------------------------------------------------------------
Gtk::Widget* Renderer::render(const html::NodePtr& root) {
    // إعداد الأنماط البصرية مرة واحدة.
    ensure_css();

    if (!root) {
        return render_children(std::make_shared<html::Node>());
    }

    // العنصر الجذري: حاوية عمودية تحتوي على كل الأبناء.
    return render_children(root);
}

}  //
