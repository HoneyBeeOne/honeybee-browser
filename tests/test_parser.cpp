// =============================================================
// الملف: اختبارات محلل إتش تي إم إل
// الغرض: التحقق من أن المحلل يُنتج الشجرة المتوقعة.
//
// ما نختبره؟
// النص الفارغ.
// النص العادي بلا وسوم.
// وسم بسيط مع نص.
// وسمان متداخلان.
// وسم فارغ بلا إغلاق.
// سمات الوسم.
// تعليقات إتش تي إم إل.
// =============================================================

// استيراد أدوات الاختبار.
#include <catch2/catch_test_macros.hpp>

// استيراد المحلل والعقدة.
#include "renderer/html/parser.hpp"

// اسم مختصر لأنواع المحلل.
using honeybee::html::NodeType;
using honeybee::html::parse;

// اختبار النص الفارغ.
// نتوقع أن تُعاد شجرة فيها جذر فقط، بلا أبناء.
TEST_CASE("parse returns root-only tree for empty input", "[parser]") {
    const auto result = parse("");
    REQUIRE(result.success);
    REQUIRE(result.root != nullptr);
    REQUIRE(result.root->children.empty());
}

// اختبار نص عادي بلا وسوم.
// نتوقع أن يُضاف النص كعقدة نص داخل الجذر.
TEST_CASE("parse handles plain text without tags", "[parser]") {
    const auto result = parse("مرحبا بالعالم");
    REQUIRE(result.success);
    REQUIRE(result.root->children.size() == 1);

    const auto& text_node = result.root->children[0];
    REQUIRE(text_node->is_text());
    REQUIRE(text_node->text == "مرحبا بالعالم");
}

// اختبار وسم بسيط مع نص.
// نتوقع أن يكون الجذر فيه عقدة عنصر واحدة، وداخلها عقدة نص.
TEST_CASE("parse handles a simple element with text", "[parser]") {
    const auto result = parse("<p>مرحبا</p>");
    REQUIRE(result.success);
    REQUIRE(result.root->children.size() == 1);

    const auto& p_node = result.root->children[0];
    REQUIRE(p_node->is_element());
    REQUIRE(p_node->tag == "p");
    REQUIRE(p_node->children.size() == 1);

    const auto& text_node = p_node->children[0];
    REQUIRE(text_node->is_text());
    REQUIRE(text_node->text == "مرحبا");
}

// اختبار وسمان متداخلان.
// نتوقع أن يكون العنصر الخارجي هو الفقرة، وداخله عنصر
// التمييز، وداخله نص.
TEST_CASE("parse handles nested elements", "[parser]") {
    const auto result = parse("<p>مرحبا <b>بالعالم</b></p>");
    REQUIRE(result.success);
    REQUIRE(result.root->children.size() == 1);

    const auto& p_node = result.root->children[0];
    REQUIRE(p_node->tag == "p");
    REQUIRE(p_node->children.size() == 2);

    const auto& text_node = p_node->children[0];
    REQUIRE(text_node->is_text());
    REQUIRE(text_node->text == "مرحبا");

    const auto& b_node = p_node->children[1];
    REQUIRE(b_node->is_element());
    REQUIRE(b_node->tag == "b");
    REQUIRE(b_node->children.size() == 1);
    REQUIRE(b_node->children[0]->text == "بالعالم");
}

// اختبار وسم فارغ، وهو الوسم الذي لا يحتاج إغلاقاً.
// نتوقع أن يُضاف إلى الشجرة مباشرة، بلا أبناء.
TEST_CASE("parse handles void elements", "[parser]") {
    const auto result = parse("قبل<br>بعد");
    REQUIRE(result.success);
    REQUIRE(result.root->children.size() == 3);

    REQUIRE(result.root->children[0]->is_text());
    REQUIRE(result.root->children[0]->text == "قبل");

    REQUIRE(result.root->children[1]->is_element());
    REQUIRE(result.root->children[1]->tag == "br");
    REQUIRE(result.root->children[1]->children.empty());

    REQUIRE(result.root->children[2]->is_text());
    REQUIRE(result.root->children[2]->text == "بعد");
}

// اختبار سمات الوسم.
// نتوقع أن تُحلَّل السمات إلى خريطة، وأن تكون القيم صحيحة.
TEST_CASE("parse handles attributes", "[parser]") {
    const auto result = parse(
        "<a href=\"https://example.com\">رابط</a>");
    REQUIRE(result.success);
    REQUIRE(result.root->children.size() == 1);

    const auto& a_node = result.root->children[0];
    REQUIRE(a_node->tag == "a");
    REQUIRE(a_node->attributes.count("href") == 1);
    REQUIRE(a_node->attributes.at("href") == "https://example.com");
}

// اختبار أن الوسوم غير حساسة لحالة الأحرف.
// نتوقع أن يُطبَّع اسم الوسم إلى أحرف صغيرة.
TEST_CASE("parse lowercases tag names", "[parser]") {
    const auto result = parse("<P>مرحبا</P>");
    REQUIRE(result.success);
    REQUIRE(result.root->children.size() == 1);
    REQUIRE(result.root->children[0]->tag == "p");
}

// اختبار التعليقات.
// نتوقع أن تُتجاهل التعليقات كلياً.
TEST_CASE("parse ignores comments", "[parser]") {
    const auto result = parse("قبل<!-- تعليق -->بعد");
    REQUIRE(result.success);
    REQUIRE(result.root->children.size() == 2);

    REQUIRE(result.root->children[0]->text == "قبل");
    REQUIRE(result.root->children[1]->text == "بعد");
}
