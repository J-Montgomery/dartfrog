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
                                .select([](const int &x) { return x * 10; })
                                .execute();
    EXPECT_EQ(results.elements, (std::vector<int>{10, 20, 30}));
}

TEST(QueryTest, JoinIdentity) {
    using KV_Type = std::pair<int, std::string>;
    Relation<int> source = Relation<int>::from_vec({1, 2, 3});
    Relation<KV_Type> lookup =
        Relation<KV_Type>::from_vec({{1, "one"}, {2, "two"}, {3, "three"}});

    Relation<KV_Type> result =
        query(std::move(source))
            .join(std::move(lookup), [](const int &x) { return x; })
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
            .antijoin(std::move(exclusion), [](const int &x) { return x; })
            .execute();

    EXPECT_EQ(result.elements, (std::vector<int>{1, 3}));
}

TEST(QueryTest, WhereSelect) {
    Relation<int> source = Relation<int>::from_vec({1, 2, 3, 4, 5});
    Relation<int> result = query(std::move(source))
                               .where([](const int &x) { return x % 2 == 0; })
                               .select([](const int &x) { return x * 100; })
                               .execute();

    EXPECT_EQ(result.elements, (std::vector<int>{200, 400}));
}

TEST(QueryTest, JoinWhereSelect) {
    Relation<int> source = Relation<int>::from_vec({1, 2, 3});
    Relation<std::pair<int, int>> lookup =
        Relation<std::pair<int, int>>::from_vec({{1, 10}, {2, 20}, {3, 30}});

    Relation<int> result =
        query(std::move(source))
            .join(std::move(lookup), [](const int &x) { return x; })
            .where([](const std::pair<int, int> &p) { return p.second > 10; })
            .select([](const std::pair<int, int> &p) { return p.second; })
            .execute();

    EXPECT_EQ(result.elements, (std::vector<int>{20, 30}));
}

TEST(QueryTest, RecursiveTransitiveClosure) {
    using Edge = std::pair<int, int>;
    Relation<Edge> edges = Relation<Edge>::from_vec({{1, 2}, {2, 3}});
    Relation<Edge> step_relation = Relation<Edge>::from_vec({{1, 2}, {2, 3}});

    Relation<Edge> result =
        query(std::move(edges))
            .join(std::move(step_relation),
                  [](const Edge &e) { return e.second; })
            .select([](const std::pair<Edge, int> &p) -> Edge {
                return {p.first.first, p.second};
            })
            .recursive()
            .execute();

    const std::vector<Edge> &elems = result.elements;
    EXPECT_TRUE(std::find(elems.begin(), elems.end(), Edge{1, 3}) !=
                elems.end());
}

TEST(QueryTest, HierarchialTagQuery) {
    using TagEdge = std::pair<std::string, std::string>;
    Relation<TagEdge> hierarchy =
        Relation<TagEdge>::from_vec({{"Pet", "Animal"}, {"Dog", "Pet"}});

    Relation<TagEdge> hierarchy_copy = hierarchy;

    Relation<TagEdge> closure =
        query(std::move(hierarchy))
            .join(std::move(hierarchy_copy),
                  [](const TagEdge &e) { return e.second; })
            .select([](const std::pair<TagEdge, std::string> &p) -> TagEdge {
                return {p.first.first, p.second};
            })
            .recursive()
            .execute();

    Relation<std::string> result =
        query(std::move(closure))
            .where([](const TagEdge &e) { return e.second == "Animal"; })
            .select([](const TagEdge &e) { return e.first; })
            .execute();

    const std::vector<std::string> &elems = result.elements;
    EXPECT_TRUE(std::find(elems.begin(), elems.end(), "Pet") != elems.end());
    EXPECT_TRUE(std::find(elems.begin(), elems.end(), "Dog") != elems.end());
}

TEST(QueryTest, GroupByCount) {
    using KV = std::pair<int, std::string>;
    Relation<KV> source =
        Relation<KV>::from_vec({{1, "a"}, {1, "b"}, {2, "c"}});

    Relation<std::pair<int, size_t>> result =
        query(std::move(source))
            .group_by([](const KV &kv) { return kv.first; })
            .count();

    EXPECT_EQ(result.elements.size(), 2u);
    EXPECT_EQ(result.elements[0], (std::pair<int, size_t>{1, 2}));
    EXPECT_EQ(result.elements[1], (std::pair<int, size_t>{2, 1}));
}

TEST(QueryTest, GroupByAggregate) {
    using KV = std::pair<int, int>;
    Relation<KV> source =
        Relation<KV>::from_vec({{1, 10}, {1, 20}, {2, 5}, {2, 15}});

    Relation<std::pair<int, int>> result =
        query(std::move(source))
            .group_by([](const KV &kv) { return kv.first; })
            .aggregate([](int, std::span<const KV> rows) {
                int sum = 0;
                for (const KV &kv : rows) {
                    sum += kv.second;
                }

                return sum;
            });

    EXPECT_EQ(result.elements.size(), 2u);
    EXPECT_EQ(result.elements[0], (std::pair<int, int>{1, 30}));
    EXPECT_EQ(result.elements[1], (std::pair<int, int>{2, 20}));
}

TEST(QueryTest, ConstexprWhereSelect) {
    Relation<int> source = Relation<int>::from_vec({1, 2, 3, 4, 5});
    Relation<int> result = query(std::move(source))
                               .where([](const int &x) { return x > 2; })
                               .select([](const int &x) { return x * 10; })
                               .execute();
    EXPECT_EQ(result.elements, (std::vector<int>{30, 40, 50}));
}

TEST(QueryTest, ConstexprTransitiveClosure) {
    using Edge = std::pair<int, int>;
    Relation<Edge> base_edges =
        Relation<Edge>::from_vec({{1, 3}, {2, 3}, {3, 4}});

    Variable<Edge> expected_var;
    expected_var.insert(Relation<Edge>::from_vec({{1, 2}, {2, 3}, {3, 4}}));
    Relation<Edge> step_edges_raw =
        Relation<Edge>::from_vec({{1, 2}, {2, 3}, {3, 4}});

    while (expected_var.changed()) {
        std::vector<Edge> derived;
        for (const Edge &e : expected_var.recent()) {
            for (const Edge &f : step_edges_raw.elements) {
                if (e.second == f.first) {
                    derived.emplace_back(e.first, f.second);
                }
            }
        }
        expected_var.insert(Relation<Edge>::from_vec(std::move(derived)));
    }

    Relation<Edge> expected = std::move(expected_var).complete();

    Relation<Edge> step_edges_dsl =
        Relation<Edge>::from_vec({{1, 3}, {2, 3}, {3, 4}});
    Relation<Edge> dsl_result =
        query(std::move(base_edges))
            .join(std::move(step_edges_dsl),
                  [](const Edge &e) { return e.second; })
            .select([](const std::pair<Edge, int> &p) -> Edge {
                return {p.first.first, p.second};
            })
            .recursive()
            .execute();

    const std::vector<Edge> &elems = dsl_result.elements;
    EXPECT_TRUE(std::find(elems.begin(), elems.end(), Edge{1, 3}) !=
                elems.end());
    EXPECT_TRUE(std::find(elems.begin(), elems.end(), Edge{1, 4}) !=
                elems.end());
    EXPECT_TRUE(std::find(elems.begin(), elems.end(), Edge{2, 4}) !=
                elems.end());
}