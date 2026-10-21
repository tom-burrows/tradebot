add_executable(
  base_test
  base_test.cpp
)
target_link_libraries(
  base_test
  PRIVATE
  GTest::gtest_main
)

## This is how you link in new modules/libraries etc for testing.
target_link_libraries(
    base_test
    PRIVATE
    engine_core
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(base_test)