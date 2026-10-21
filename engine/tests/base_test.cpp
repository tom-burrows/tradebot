#include <gtest/gtest.h>
#include "core/version.hpp"

namespace {
    TEST (BaseTest, BasicAssertions) {
        EXPECT_STRNE("Hello", "World");
        EXPECT_EQ(3 * 3, 9);
    }

    TEST (BaseTest, VersionCheck) {
        Version v;
        EXPECT_EQ(ENGINE_VERSION, v.getVersion());
    }

    TEST (BaseTest, IntentionalFailureForGithubActions) {
        EXPECT_EQ(1, 2);
    }
}