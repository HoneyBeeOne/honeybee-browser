add_executable(honeybee_tests
  test_version.cpp
  test_url.cpp
  test_navigation_history.cpp
  test_page_cache.cpp
  test_net.cpp
  test_dom.cpp
  test_selectors.cpp
  test_stylesheet.cpp
  test_cascade.cpp
  test_hover_sensitivity.cpp
  test_ua_stylesheet.cpp
  test_layout.cpp
  test_paint.cpp
  test_lexbor_smoke.cpp
  test_convert.cpp
  test_css_sources.cpp
  test_preview.cpp
)

target_link_libraries(honeybee_tests PRIVATE
  honeybee_lib
  lexbor_static
  Catch2::Catch2WithMain
)

add_test(NAME honeybee_tests COMMAND honeybee_tests)
