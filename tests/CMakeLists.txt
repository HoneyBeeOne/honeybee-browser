add_executable(honeybee_tests
  test_version.cpp
)

target_link_libraries(honeybee_tests PRIVATE
  honeybee_lib
  Catch2::Catch2WithMain
)

add_test(NAME honeybee_tests COMMAND honeybee_tests)
