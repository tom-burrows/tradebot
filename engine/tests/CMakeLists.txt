add_executable(
  base_test
  base_test.cpp
)

add_executable(
    sanitizer_test
    sanitizer_test.cpp
)

## This is how you link in new modules/libraries etc for testing.
target_link_libraries(
    base_test
    PRIVATE
    engine_core
    GTest::gtest_main
)

## This is added to ensure that sanitizers catch something in builds/github actions
## Only uncomment if you want the builds to fail
target_link_libraries(
    sanitizer_test
    PRIVATE
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(base_test)
gtest_discover_tests(sanitizer_test)
