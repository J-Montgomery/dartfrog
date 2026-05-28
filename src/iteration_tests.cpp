#include <gtest/gtest.h>
#include <string>
#include <vector>

#include <dartfrog.hpp>

TEST(IterationTest, ChangedReturnsFalseWhenAllStable) {
    Iteration iter;
    auto v = iter.variable<int>("v");
    EXPECT_FALSE(iter.changed());
}

TEST(IterationTest, ChangedReturnsTrueWhenVariableHasNewData) {
    Iteration iter;
    auto v = iter.variable<int>("v");
    v->insert(Relation<int>::from_vec({1, 2}));
    EXPECT_TRUE(iter.changed());
}

TEST(IterationTest, FixedPointConverges) {
    Iteration iter;
    auto v = iter.variable<int>("v");

    v->insert(Relation<int>::from_vec({1}));
    EXPECT_TRUE(iter.changed());

    EXPECT_FALSE(iter.changed());
    EXPECT_FALSE(iter.changed());
}

TEST(IterationTest, MultipleVariables) {
    Iteration iter;
    auto a = iter.variable<int>("a");
    auto b = iter.variable<int>("b");

    a->insert(Relation<int>::from_vec({1}));
    EXPECT_TRUE(iter.changed());

    b->insert(Relation<int>::from_vec({2}));
    EXPECT_TRUE(iter.changed());

    EXPECT_FALSE(iter.changed());
}

TEST(IterationTest, VariableIndistinct) {
    Iteration iter;
    auto v = iter.variable_indistinct<int>("v");
    EXPECT_FALSE(v->distinct);
}

TEST(IterationTest, RecordStatsTo) {
    Iteration iter;
    auto v = iter.variable<int>("v");
    v->insert(Relation<int>::from_vec({1}));

    std::ostringstream oss;
    iter.record_stats_to(oss);
    iter.changed();

    std::string output = oss.str();
    EXPECT_TRUE(output.find("Variable, Round, Stable Count, Recent Count") != std::string::npos);
    EXPECT_TRUE(output.find("\"v\"") != std::string::npos);
}
