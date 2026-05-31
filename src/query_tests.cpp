#include <gtest/gtest.h>

#include <span>
#include <string>
#include <utility>
#include <vector>

#include "query.hpp"

TEST(QueryTest, SelectAllNoOps) {
    Relation<int> source = Relation<int>::from_vec({3, 1, 2});
    Relation<int> result = query(std::move(source)).execute();

    EXPECT_EQ(result.elements, (std::vector<int>{1, 2, 3}));
}

TEST(QueryTest, WhereFilter) {
    Relation<int> source = Relation<int>::from_vec({1, 2, 3, 4, 5});
    Relation<int> result = query(std::move(source))
                               .where([](const int &x) { return x > 3; })
                               .execute();

    EXPECT_EQ(result.elements, (std::vector<int>{4, 5}));
}

TEST(QueryTest, SelectProjection) {
    Relation<int> source = Relation<int>::from_vec({1, 2, 3});
    Relation<int> results = query(std::move(source))
                            .select([](const int &x) { return x * 10; }).execute();
    EXPECT_EQ(results.elements, (std::vector<int>{10, 20, 30}));
}

TEST(QueryTest, JoinIdentity) {
    using KV_Type = std::pair<int, std::string>;
    Relation<int> source = Relation<int>::from_vec({1, 2, 3});
    Relation<KV_Type> lookup = Relation<KV_Type>::from_vec(
        {{1, "one"}, {2, "two"}, {3, "three"}}
    );

    Relation<KV_Type> result = query(std::move(source))
        .join(std::move(lookup), [](const int &x) { return x;})
        .execute();

    ASSERT_EQ(result.elements.size(), 3);
    EXPECT_EQ(result.elements[0], (KV_Type{1, "one"}));
    EXPECT_EQ(result.elements[1], (KV_Type{2, "two"}));
    EXPECT_EQ(result.elements[2], (KV_Type{3, "three"}));
}

TEST(QueryTest, AntiJoin) {
    Relation<int> source = Relation<int>::from_vec({1, 2, 3});
    Relation<std::pair<int, std::monostate>> exclusion =
        Relation<std::pair<int, std::monostate>>::from_vec({{2, {}}});

    Relation<int> result =
        query(std::move(source))
            .antijoin(std::move(exclusion), [](const int &x) { return x;})
            .execute();

    EXPECT_EQ(result.elements, (std::vector<int>{1, 3}));
}

TEST(QueryTest, SelectWhere) {
    Relation<int> source = Relation<int>::from_vec({1, 2, 3, 4, 5});
    Relation<int> result = query(std::move(source))
        .where([](const int &x) { return x % 2 == 0; })
        .select([](const int &x) { return x * 100; })
        .execute();

    EXPECT_EQ(result.elements, (std::vector<int>{200, 400}));
}