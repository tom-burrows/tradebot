#include <gtest/gtest.h>

#include "refresher/ScopedFile.hpp"

namespace {

TEST(RefresherTest, RAIITest) {
  ScopedFile sf;

  sf.foo(false);

  std::cout << "Words" << std::endl;
}
}  // namespace
