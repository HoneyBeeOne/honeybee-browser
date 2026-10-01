// =============================================================
// الملف: تنفيذ محلل إتش تي إم إل
// الغرض: تنفيذ دالة التحليل ومكوناتها.
//
// كيف يعمل المحلل؟
// نمر على النص حرفاً حرفاً. وعندما نصل إلى وسم، نقرأه.
// وعندما نصل إلى نص، نقرأه. ونبني الشجرة تدريجياً،
// باستخدام مكدس من العقد المفتوحة.
//
// ما المكدس؟
// المكدس هو بنية بيانات تدعم الإضافة والحذف من أعلى فقط.
// نستخدمه لتتبع الوسوم المفتوحة: كل وسم مفتوح يُضاف إلى
// أعلى المكدس، وكل وسم مغلق يُزيل الوسم المطابق من الأعلى.
//
// ما الوسوم الفارغة؟
// هي وسوم لا تحتاج إلى وسم إغلاق، مثل وسم الفاصل.
// وتُضاف إلى الشجرة مباشرة، دون إضافتها إلى المكدس.
// =============================================================

// استيراد إعلان المحلل.
#include "renderer/html/parser.hpp"

// استيراد الأدوات المعيارية:
// حروف، لتحويل النص إلى أحرف صغيرة في المقارنات.
// مجموعات، لاستخدام مجموعة من الوسوم الفارغة.
#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>

namespace honeybee::html {

namespace {

// هل الحرف الممرر يمثل فراغاً؟
// الفراغ يشمل: المسافة، والجدولة، والسطر الجديد،
// والعودة إلى بداية السطر.
bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// إزالة الفراغات من بداية النص ونهايته.
std::string trim(const std::string& s) {
    std::size_t start = 0;
    std::size_t end = s.size();

    while (start < end && is_space(s[start])) {
        ++start;
    }
    while (end > start && is_space(s[end - 1])) {
        --end;
    }

    return s.substr(start, end - start);
}

// تحويل النص إلى أحرف صغيرة.
// نستخدم هذا لمقارنة أسماء الوسوم، لأن إتش تي إم إل
// لا يفرق بين الحروف الكبيرة والصغيرة في أسماء الوسوم.
std::string to_lower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return result;
}

// هل الوسم فارغ؟
// الوسوم الفارغة لا تحتاج إلى وسم إغلاق.
bool is_void_element(const std::string& tag) {
    static const std::set<std::string> void_tags = {
        "br", "hr", "img", "input", "meta", "link"
    };
    return void_tags.count(to_lower(tag)) > 0;
}

}  // النهاية


namespace {

// تحليل سمات الوسم.
//
// المدخل: النص الموجود داخل وسم البداية، بعد اسم الوسم.
// مثال: إذا كان الوسم هو الرابط مع سمة الهدف، فإن النص
// الممرر يكون:
// href="https://example.com"
//
// المُخرج: خريطة من اسم السمة إلى قيمتها.
//
// طريقة العمل:
// نمر على النص حرفاً حرفاً. وعندما نجد اسماً، نقرأه.
// ثم نتخطى الفراغات وعلامة التساوي. ثم نقرأ القيمة بين
// علامتي اقتباس. ونُضيف الزوج إلى الخريطة.
std::map<std::string, std::string> parse_attributes(
    const std::string& s) {
    std::map<std::string, std::string> attributes;

    std::size_t i = 0;
    const std::size_t n = s.size();

    while (i < n) {
        // تخطي الفراغات.
        while (i < n && is_space(s[i])) {
            ++i;
        }
        if (i >= n) {
            break;
        }

        // قراءة اسم السمة.
        std::string name;
        while (i < n && !is_space(s[i]) && s[i] != '=' &&
               s[i] != '/' && s[i] != '>') {
            name += s[i];
            ++i;
        }

        // تخطي الفراغات قبل علامة التساوي.
        while (i < n && is_space(s[i])) {
            ++i;
        }

        // هل توجد قيمة؟
        if (i < n && s[i] == '=') {
            ++i;  // تخطي علامة التساوي.

            // تخطي الفراغات بعد علامة التساوي.
            while (i < n && is_space(s[i])) {
                ++i;
            }

            // قراءة القيمة.
            std::string value;

            if (i < n && (s[i] == '"' || s[i] == '\'')) {
                // قيمة بين علامتي اقتباس.
                const char quote = s[i];
                ++i;
                while (i < n && s[i] != quote) {
                    value += s[i];
                    ++i;
                }
                if (i < n) {
                    ++i;  // تخطي علامة الاقتباس المُغلقة.
                }
            } else {
                // قيمة بلا اقتباس.
                while (i < n && !is_space(s[i]) && s[i] != '>') {
                    value += s[i];
                    ++i;
                }
            }

            if (!name.empty()) {
                attributes[to_lower(name)] = value;
            }
        } else if (!name.empty()) {
            // سمة بلا قيمة، مثل: disabled.
            attributes[to_lower(name)] = "";
        }

        // تخطي الفراغات قبل السمة التالية.
        while (i < n && is_space(s[i])) {
            ++i;
        }
    }

    return attributes;
}

}  // النهاية


namespace {

// قراءة اسم الوسم من النص، بدءاً من الموضع الممرر.
// تُحدَّث قيمة الموضع لتشير إلى ما بعد اسم الوسم.
std::string read_tag_name(const std::string& html, std::size_t& i) {
    std::string name;
    const std::size_t n = html.size();

    while (i < n && !is_space(html[i]) && html[i] != '>' &&
           html[i] != '/') {
        name += html[i];
        ++i;
    }

    return name;
}

}  // النهاية

// الدالة العامة: تحليل نص إتش تي إم إل وإنتاج شجرة عقد.
ParseResult parse(const std::string& html) {
    ParseResult result;

    // إنشاء العقدة الجذرية.
    // سيكون اسمها الجذري، وتمثل الحاوية العامة للصفحة.
    result.root = std::make_shared<Node>();
    result.root->type = NodeType::Element;
    result.root->tag = "root";

    // المكدس: يبدأ بالجذر.
    // نستخدم متجهاً كمكدس، مع الدالتين push_back و pop_back.
    std::vector<NodePtr> stack;
    stack.push_back(result.root);

    // الموضع الحالي في النص.
    std::size_t i = 0;
    const std::size_t n = html.size();

    // الحلقة الرئيسية: نمشي على النص حرفاً حرفاً.
    while (i < n) {
        // هل وصلنا إلى وسم؟
        if (html[i] == '<') {
            // هل هو وسم إغلاق؟
            if (i + 1 < n && html[i + 1] == '/') {
                // قراءة اسم الوسم.
                i += 2;
                const std::string name = read_tag_name(html, i);

                // تخطي حتى نهاية الوسم.
                while (i < n && html[i] != '>') {
                    ++i;
                }
                if (i < n) {
                    ++i;  // تخطي علامة النهاية.
                }

                // إزالة الوسم من المكدس.
                // نتحقق أولاً أن اسم الوسم يطابق الوسم العلوي.
                // لو لم يطابق، نتجاهل وسم الإغلاق (متسامحون).
                if (stack.size() > 1) {
                    const auto& top = stack.back();
                    if (to_lower(top->tag) == to_lower(name)) {
                        stack.pop_back();
                    }
                }
            }
            // هل هو تعليق؟
            else if (i + 3 < n && html[i + 1] == '!' &&
                     html[i + 2] == '-' && html[i + 3] == '-') {
                // تخطي حتى نهاية التعليق.
                i += 4;
                while (i + 2 < n &&
                       !(html[i] == '-' && html[i + 1] == '-' &&
                         html[i + 2] == '>')) {
                    ++i;
                }
                i += 3;
            }
            // هل هو تعريف نوع المستند؟
            else if (i + 1 < n && html[i + 1] == '!') {
                // تخطي حتى نهاية التعريف.
                while (i < n && html[i] != '>') {
                    ++i;
                }
                if (i < n) {
                    ++i;
                }
            }
            // وسم بداية عادي.
            else {
                ++i;  // تخطي علامة البداية.

                // قراءة اسم الوسم.
                const std::string name = read_tag_name(html, i);

                // قراءة نص السمات (حتى نهاية الوسم).
                std::string attrs_text;
                while (i < n && html[i] != '>') {
                    attrs_text += html[i];
                    ++i;
                }
                if (i < n) {
                    ++i;  // تخطي علامة النهاية.
                }

                // إذا كان اسم الوسم فارغاً، نتجاهله.
                if (name.empty()) {
                    continue;
                }

                // إنشاء عقدة جديدة للوسم.
                auto node = std::make_shared<Node>();
                node->type = NodeType::Element;
                node->tag = to_lower(name);
                node->attributes = parse_attributes(attrs_text);

                // إضافتها إلى أبناء الوسم الحالي.
                stack.back()->children.push_back(node);

                // هل الوسم فارغ؟
                // إن كان فارغاً، لا نُضيفه إلى المكدس.
                // وإن كان عادياً، نُضيفه ليصبح الأب الحالي.
                if (!is_void_element(name)) {
                    stack.push_back(node);
                }
            }
        }
        // نص عادي.
        else {
            // جمع النص حتى نصل إلى وسم أو نهاية النص.
            std::string text;
            while (i < n && html[i] != '<') {
                text += html[i];
                ++i;
            }

            // إزالة الفراغات.
            const std::string trimmed = trim(text);

            // إن كان النص غير فارغ، نُضيفه كعقدة نص.
            if (!trimmed.empty()) {
                auto text_node = std::make_shared<Node>();
                text_node->type = NodeType::Text;
                text_node->text = trimmed;
                stack.back()->children.push_back(text_node);
            }
        }
    }

    return result;
}

}  //
