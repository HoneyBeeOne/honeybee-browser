# المكتبة المشتركة: كود نقي قابل للاختبار (بدون GTK)
add_library(honeybee_lib
  base/version.cpp
  base/url.cpp
  net/client.cpp
  renderer/html/document.cpp
  renderer/html/preview.cpp
)

target_include_directories(honeybee_lib PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

# ربط CURL كمكتبة عامة، حتى يرثها التنفيذي والاختبارات
target_link_libraries(honeybee_lib PUBLIC
  PkgConfig::CURL
)

# lexbor is a private implementation detail of the HTML parser.
# Consumers of honeybee_lib do not need to know about it, but CMake
# propagates the link dependency to final executables.
target_link_libraries(honeybee_lib PRIVATE
  lexbor_static
)

# التنفيذي الرئيسي
add_executable(honeybee
  main.cpp
  browser/app.cpp
  browser/window.cpp
  browser/async_fetcher.cpp
)

target_include_directories(honeybee PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(honeybee PRIVATE
  honeybee_lib
  PkgConfig::GTKMM4
)

set_target_properties(honeybee PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
