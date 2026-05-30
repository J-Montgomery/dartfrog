#include <gtest/gtest.h>
#include <string>
#include <vector>

#include <dartfrog.hpp>

TEST(IterationTest, ChangedReturnsFalseWhenAllStable) {
    auto [iter, v] = Iteration{}.variable<int>();
    EXPECT_FALSE(iter.changed());
}

TEST(IterationTest, ChangedReturnsTrueWhenVariableHasNewData) {
    auto [iter, v] = Iteration{}.variable<int>();
    
    v->insert(Relation<int>::from_vec({1, 2}));
    EXPECT_TRUE(iter.changed());
}

TEST(IterationTest, FixedPointConverges) {
    auto [iter, v] = Iteration{}.variable<int>();

    v->insert(Relation<int>::from_vec({1}));
    EXPECT_TRUE(iter.changed());

    EXPECT_FALSE(iter.changed());
    EXPECT_FALSE(iter.changed());
}

TEST(IterationTest, MultipleVariables) {
    auto [iter1, a] = Iteration{}.variable<int>();
    auto [iter, b]  = std::move(iter1).variable<int>();

    a->insert(Relation<int>::from_vec({1}));
    EXPECT_TRUE(iter.changed());

    b->insert(Relation<int>::from_vec({2}));
    EXPECT_TRUE(iter.changed());

    EXPECT_FALSE(iter.changed());
}

TEST(IterationTest, VariableIndistinct) {
    auto [iter, v] = Iteration{}.variable_indistinct<int>();
    EXPECT_FALSE(v->distinct);
}