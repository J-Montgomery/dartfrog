#include <gtest/gtest.h>
#include <string>
#include <vector>

#include <dartfrog.hpp>

using namespace df;

TEST(RelationTest, FromVecHandlesDuplicatesAndSorting) {
    std::vector<int> input = {5, 1, 3, 3, 1, 5};
    auto rel = Relation<int>::from_vec(std::move(input));

    std::vector<int> expected = {1, 3, 5};
    EXPECT_EQ(rel.elements, expected);
}

TEST(RelationTest, MergeTwoRelations) {
    auto rel1 = Relation<int>::from_vec({1, 2, 3});
    auto rel2 = Relation<int>::from_vec({3, 4, 5});

    auto result = std::move(rel1).merge(std::move(rel2));

    std::vector<int> expected = {1, 2, 3, 4, 5};
    EXPECT_EQ(result.elements, expected);
}

TEST(RelationTest, FromMapTransformation) {
    auto rel_ints = Relation<int>::from_vec({1, 2, 3});

    auto rel_strs = Relation<std::string>::from_map(
        rel_ints, [](int i) { return "val" + std::to_string(i); });

    std::vector<std::string> expected = {"val1", "val2", "val3"};
    EXPECT_EQ(rel_strs.elements, expected);
}

TEST(RelationTest, FromIterMoveSemantics) {
    std::vector<std::string> sources = {"apple", "banana"};

    auto rel = Relation<std::string>::from_iter(sources);

    EXPECT_EQ(rel.elements.size(), 2);
    EXPECT_EQ(rel.elements[0], "apple");
    EXPECT_TRUE(sources[0].empty());
}

TEST(RelationTest, FromVecAlreadySorted) {
    auto rel = Relation<int>::from_vec({1, 2, 3, 4});
    EXPECT_EQ(rel.elements, (std::vector<int>{1, 2, 3, 4}));
}

TEST(RelationTest, FromVecEmpty) {
    auto rel = Relation<int>::from_vec({});
    EXPECT_TRUE(rel.elements.empty());
}

TEST(RelationTest, FromVecSingleElement) {
    auto rel = Relation<int>::from_vec({42});
    EXPECT_EQ(rel.elements, (std::vector<int>{42}));
}

TEST(RelationTest, MergeWithEmpty) {
    auto r1 = Relation<int>::from_vec({1, 2, 3});
    auto r2 = Relation<int>{};
    auto r3 = std::move(r1).merge(std::move(r2));
    EXPECT_EQ(r3.elements, (std::vector<int>{1, 2, 3}));
}

TEST(RelationTest, FromMap) {
    auto input = Relation<int>::from_vec({1, 2, 3});
    auto output = Relation<int>::from_map(input, [](int x) { return x * 10; });
    EXPECT_EQ(output.elements, (std::vector<int>{10, 20, 30}));
}

TEST(RelationTest, FromIter) {
    std::vector<int> v = {3, 1, 2, 1};
    auto rel = Relation<int>::from_iter(v);
    EXPECT_EQ(rel.elements, (std::vector<int>{1, 2, 3}));
}

TEST(RelationTest, DefaultConstructedIsEmpty) {
    Relation<int> rel;
    EXPECT_TRUE(rel.elements.empty());
}
