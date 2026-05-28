#include <gtest/gtest.h>

#include "dartfrog.hpp"

TEST(UsageTest, TransitiveClosureOneStep) {
    // edge(1,2), edge(2,3) => path(1,2), path(2,3), path(1,3)
    Iteration iter;
    auto edge = iter.variable<std::pair<int, int>>("edge");
    auto path = iter.variable<std::pair<int, int>>("path");

    // Seed edges
    edge->insert(Relation<std::pair<int, int>>::from_vec({{1, 2}, {2, 3}}));

    while (iter.changed()) {
        // path(x, z) :- edge(x, z)
        path->from_join(
            *edge, *edge,
            *path,
            [](int x, int z, int _) { return std::pair{x, z}; }
        );
    }

    auto result = std::move(*path).complete();
    EXPECT_EQ(result.elements, (std::vector<std::pair<int, int>>{{1, 2}, {2, 3}}));
}

TEST(UsageTest, JoinTwoRelations) {
    // parent(john, mary), parent(mary, ann)
    // grandparent(X, Z) :- parent(X, Y), parent(Y, Z)
    Iteration iter;
    auto parent = iter.variable<std::pair<std::string, std::string>>("parent");
    auto grandparent = iter.variable<std::pair<std::string, std::string>>("grandparent");

    parent->insert(Relation<std::pair<std::string, std::string>>::from_vec({
        {"john", "mary"},
        {"mary", "ann"},
        {"bob", "carol"}
    }));

    while (iter.changed()) {
        grandparent->from_join(
            *parent, *parent,
            *grandparent,
            [](const std::string &x, const std::string &y, const std::string &z) {
                return std::pair{x, z};
            }
        );
    }

    auto result = std::move(*grandparent).complete();
    EXPECT_EQ(result.elements, (std::vector<std::pair<std::string, std::string>>{
        {"john", "ann"}}));
}

TEST(UsageTest, AntijoinExample) {
    // student(1), student(2), student(3)
    // graduated(2)
    // active_student(X) :- student(X), not graduated(X)
    Iteration iter;
    auto student = iter.variable<int>("student");
    auto graduated = iter.variable<std::pair<int, Unit>>("graduated");
    auto active = iter.variable<int>("active");

    student->insert(Relation<int>::from_vec({1, 2, 3}));
    graduated->insert(Relation<std::pair<int, Unit>>::from_vec({{2, {}}}));

    while (iter.changed()) {
        active->from_antijoin(
            *student,
            graduated->recent_data,
            [](int x, Unit) { return x; }
        );
    }

    auto result = std::move(*active).complete();
    EXPECT_EQ(result.elements, (std::vector<int>{1, 3}));
}

TEST(UsageTest, LeapjoinExample) {
    // parent(john, mary), parent(mary, ann)
    // grandparent(X, Z) :- parent(X, Y), parent(Y, Z)
    Iteration iter;
    auto parent = iter.variable<std::pair<std::string, std::string>>("parent");
    auto grandparent = iter.variable<std::pair<std::string, std::string>>("grandparent");

    parent->insert(Relation<std::pair<std::string, std::string>>::from_vec({
        {"john", "mary"},
        {"mary", "ann"},
        {"bob", "carol"}
    }));

    while (iter.changed()) {
        // Build leapers from the parent relation
        // parent(Y, Z) extended by Y=key
        RelationLeaper<std::string, std::string> parent_rl{&parent->recent_data};

        grandparent->from_leapjoin(
            *parent,
            LeaperCollection<
                std::pair<std::string, std::string>,
                std::string,
                extend_with::ExtendWith<std::string, std::string,
                    std::pair<std::string, std::string>,
                    decltype([](const std::pair<std::string, std::string> &p) {
                        return p.second;
                    })>
            >{std::tuple{
                parent_rl.extend_with<std::pair<std::string, std::string>>(
                    [](const std::pair<std::string, std::string> &p) {
                        return p.second;
                    })
            }},
            [](const std::pair<std::string, std::string> &p, const std::string &z) {
                return std::pair{p.first, z};
            }
        );
    }

    auto result = std::move(*grandparent).complete();
    EXPECT_EQ(result.elements, (std::vector<std::pair<std::string, std::string>>{
        {"john", "ann"}}));
}

TEST(UsageTest, MapInto) {
    Iteration iter;
    auto input = iter.variable<int>("input");
    auto output = iter.variable<int>("output");

    input->insert(Relation<int>::from_vec({1, 2, 3}));

    while (iter.changed()) {
        map::map_into(*input, *output, [](int x) { return x * 10; });
    }

    auto result = std::move(*output).complete();
    EXPECT_EQ(result.elements, (std::vector<int>{10, 20, 30}));
}

TEST(UsageTest, MultipleRoundsOfFixedPoint) {
    // Simulate: reachable from 0 in a graph
    // edge(0,1), edge(1,2), edge(2,3)
    Iteration iter;
    auto edge = iter.variable<std::pair<int, int>>("edge");
    auto reachable = iter.variable<int>("reachable");

    edge->insert(Relation<std::pair<int, int>>::from_vec({
        {0, 1}, {1, 2}, {2, 3}
    }));
    reachable->insert(Relation<int>::from_vec({0}));

    int round = 0;
    while (iter.changed() && round < 10) {
        round++;
        // reachable(y) :- reachable(x), edge(x, y)
        reachable->from_join(
            *reachable, *edge,
            *reachable,
            [](int x, int y, int _) { return y; }
        );
    }

    auto result = std::move(*reachable).complete();
    EXPECT_EQ(result.elements, (std::vector<int>{0, 1, 2, 3}));
}
