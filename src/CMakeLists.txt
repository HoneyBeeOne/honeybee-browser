add_executable(honeybee
  main.cpp
  browser/app.cpp
  browser/window.cpp
)

target_include_directories(honeybee PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(honeybee PRIVATE
  PkgConfig::GTKMM4
  PkgConfig::CURL
)

set_target_properties(honeybee PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
