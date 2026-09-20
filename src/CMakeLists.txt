# Main executable
add_executable(honeybee
  main.cpp
)

target_include_directories(honeybee PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(honeybee PRIVATE
  PkgConfig::GTKMM4
  PkgConfig::CURL
)

# Output directory
set_target_properties(honeybee PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
