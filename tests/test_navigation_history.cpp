#include <catch2/catch_test_macros.hpp>

#include "browser/navigation_history.hpp"

using honeybee::browser::NavigationHistory;

TEST_CASE("NavigationHistory: push on empty", "[nav-history]") {
    NavigationHistory h;
    REQUIRE(h.empty());
    CHECK(h.size() == 0);
    CHECK(h.index() == 0);
    CHECK_FALSE(h.can_back());
    CHECK_FALSE(h.can_forward());

    h.push("https://a.example/");
    CHECK_FALSE(h.empty());
    CHECK(h.size() == 1);
    CHECK(h.index() == 0);
    CHECK_FALSE(h.can_back());
    CHECK_FALSE(h.can_forward());
    CHECK(h.current() == "https://a.example/");
}

TEST_CASE("NavigationHistory: two pushes, back enabled",
          "[nav-history]") {
    NavigationHistory h;
    h.push("A");
    h.push("B");
    CHECK(h.size() == 2);
    CHECK(h.index() == 1);
    CHECK(h.can_back());
    CHECK_FALSE(h.can_forward());
    CHECK(h.current() == "B");
}

TEST_CASE("NavigationHistory: back then forward walks the stack",
          "[nav-history]") {
    NavigationHistory h;
    h.push("A");
    h.push("B");
    h.push("C");

    auto b1 = h.back();
    REQUIRE(b1.has_value());
    CHECK(*b1 == "B");
    CHECK(h.index() == 1);
    CHECK(h.can_back());
    CHECK(h.can_forward());
    CHECK(h.current() == "B");

    auto b2 = h.back();
    REQUIRE(b2.has_value());
    CHECK(*b2 == "A");
    CHECK(h.index() == 0);
    CHECK_FALSE(h.can_back());
    CHECK(h.can_forward());
    CHECK(h.current() == "A");

    auto f1 = h.forward();
    REQUIRE(f1.has_value());
    CHECK(*f1 == "B");
    CHECK(h.index() == 1);

    auto f2 = h.forward();
    REQUIRE(f2.has_value());
    CHECK(*f2 == "C");
    CHECK(h.index() == 2);
    CHECK_FALSE(h.can_forward());
}

TEST_CASE("NavigationHistory: push after back truncates forward",
          "[nav-history]") {
    NavigationHistory h;
    h.push("A");
    h.push("B");
    h.push("C");

    (void)h.back();  // now at B
    (void)h.back();  // now at A
    CHECK(h.can_forward());

    h.push("D");
    CHECK(h.size() == 2);       // B and C are gone
    CHECK(h.index() == 1);
    CHECK_FALSE(h.can_forward());
    CHECK(h.current() == "D");

    (void)h.back();
    CHECK(h.current() == "A");
}

TEST_CASE("NavigationHistory: back at start returns nullopt",
          "[nav-history]") {
    NavigationHistory h;
    h.push("A");
    auto b = h.back();
    CHECK_FALSE(b.has_value());
    CHECK(h.index() == 0);
    CHECK(h.current() == "A");
}

TEST_CASE("NavigationHistory: forward at end returns nullopt",
          "[nav-history]") {
    NavigationHistory h;
    h.push("A");
    h.push("B");
    auto f = h.forward();
    CHECK_FALSE(f.has_value());
    CHECK(h.index() == 1);
    CHECK(h.current() == "B");
}

TEST_CASE("NavigationHistory: back/forward on empty returns nullopt",
          "[nav-history]") {
    NavigationHistory h;
    CHECK_FALSE(h.back().has_value());
    CHECK_FALSE(h.forward().has_value());
    CHECK(h.empty());
}

TEST_CASE("NavigationHistory: clear resets everything",
          "[nav-history]") {
    NavigationHistory h;
    h.push("A");
    h.push("B");
    (void)h.back();
    h.clear();
    CHECK(h.empty());
    CHECK(h.size() == 0);
    CHECK(h.index() == 0);
    CHECK_FALSE(h.can_back());
    CHECK_FALSE(h.can_forward());
}
