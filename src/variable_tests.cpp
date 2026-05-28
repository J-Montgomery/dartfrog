#include <gtest/gtest.h>
#include <string>
#include <vector>

#include <dartfrog.hpp>

TEST(VariableTest, InitiallyStable) {
    Variable<int> v("test");
    EXPECT_TRUE(v.is_stable());
    EXPECT_TRUE(v.recent().empty());
    EXPECT_EQ(v.num_stable(), 0);
}

TEST(VariableTest, InsertAndChanged) {
    Variable<int> v("test");
    v.insert(Relation<int>::from_vec({1, 2, 3}));
    EXPECT_FALSE(v.is_stable());

    bool changed = v.changed();
    EXPECT_TRUE(changed);
    EXPECT_FALSE(v.recent().empty());
    EXPECT_EQ(v.recent().size(), 3);
}

TEST(VariableTest, ChangedReturnsFalseWhenNoNewData) {
    Variable<int> v("test");
    v.insert(Relation<int>::from_vec({1, 2}));
    v.changed(); // consume

    bool changed = v.changed();
    EXPECT_FALSE(changed);
    EXPECT_TRUE(v.is_stable());
}

TEST(VariableTest, MultipleInsertBatches) {
    Variable<int> v("test");
    v.insert(Relation<int>::from_vec({1, 2}));
    v.insert(Relation<int>::from_vec({3, 4}));
    v.changed();

    // After one changed(), both batches should be merged into recent
    EXPECT_EQ(v.recent().size(), 4);
}

TEST(VariableTest, DistinctDeduplicatesAgainstStable) {
    Variable<int> v("test");
    v.insert(Relation<int>::from_vec({1, 2, 3}));
    v.changed(); // stable now contains {1, 2, 3}

    v.insert(Relation<int>::from_vec({2, 3, 4}));
    v.changed(); // should dedup 2 and 3

    EXPECT_EQ(v.recent().size(), 1);
    EXPECT_EQ(v.recent()[0], 4);
}

TEST(VariableTest, IndistinctDoesNotDeduplicate) {
    auto v = std::make_shared<Variable<int>>("test");
    v->distinct = false;
    v->insert(Relation<int>::from_vec({1, 2, 3}));
    v->changed();

    v->insert(Relation<int>::from_vec({2, 3, 4}));
    v->changed();

    // Indistinct: duplicates are not removed
    EXPECT_EQ(v->recent().size(), 3);
}

TEST(VariableTest, CompleteWhenStable) {
    Variable<int> v("test");
    v.insert(Relation<int>::from_vec({3, 1, 2}));
    v.changed();

    auto result = std::move(v).complete();
    EXPECT_EQ(result.elements, (std::vector<int>{1, 2, 3}));
}

TEST(VariableTest, CompleteWhenNotStableThrows) {
    Variable<int> v("test");
    v.insert(Relation<int>::from_vec({1, 2}));
    EXPECT_THROW(std::move(v).complete(), std::runtime_error);
}

TEST(VariableTest, ForEachStableSetIteratesBatches) {
    Variable<int> v("test");
    v.insert(Relation<int>::from_vec({1}));
    v.changed();
    v.insert(Relation<int>::from_vec({2}));
    v.changed();

    size_t count = 0;
    v.for_each_stable_set([&](std::span<const int> batch) {
        count += batch.size();
    });
    EXPECT_EQ(count, 2);
}

TEST(VariableTest, NumStableAccumulatesAcrossBatches) {
    Variable<int> v("test");
    v.insert(Relation<int>::from_vec({1, 2}));
    v.changed();
    v.insert(Relation<int>::from_vec({3, 4, 5}));
    v.changed();

    EXPECT_EQ(v.num_stable(), 5);
}

TEST(VariableTest, NameIsSet) {
    Variable<int> v("my_variable");
    EXPECT_EQ(v.name(), "my_variable");
}