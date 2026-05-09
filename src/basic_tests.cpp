#include "gtest/gtest.h"

#include "dartfrog.hpp"

class AncestryTests : public ::testing::Test {};

TEST_F(AncestryTests, RunBasicTest) {
    EXPECT_EQ(1, 1);
}
