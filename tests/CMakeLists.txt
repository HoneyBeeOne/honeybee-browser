add_executable(honeybee_tests
  test_version.cpp
  test_url.cpp
  test_net.cpp
  test_parser.cpp
)

target_link_libraries(honeybee_tests PRIVATE
  honeybee_lib
  Catch2::Catch2WithMain
)

add_test(NAME honeybee_tests COMMAND honeybee_tests)
