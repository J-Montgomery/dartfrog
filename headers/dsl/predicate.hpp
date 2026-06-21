#pragma once

#include "../dartfrog.hpp"
#include "var.hpp"

struct IPredicate {
    virtual bool step() = 0;
    virtual ~IPredicate() = default;
};

class Datalog {
    std::vector<IPredicate*> predicates;
    std::vector<std::function<void()>> evaluators;

public:
    void register_predicate(IPredicate* p) { predicates.push_back(p); }

    void solve() {
        bool changed = true;
        while (changed) {
            for (auto& rule_closure : evaluators) {
                rule_closure();
            }

            changed = false;
            for (auto* p : predicates) {
                if (p->step()) changed = true;
            }
        }
    }

    template <typename HPred, typename HV1, typename HV2,
              typename BPred, typename BV1, typename BV2>
    void add_rule(const Rule<Term<HPred, HV1, HV2>, Term<BPred, BV1, BV2>>& rule) {
        static constexpr bool direct = std::is_same_v<HV1, BV1> && std::is_same_v<HV2, BV2>;
        static constexpr bool swap   = std::is_same_v<HV1, BV2> && std::is_same_v<HV2, BV1>;
        static_assert(direct || swap, "Rule variables mismatch between Head and Body");

        evaluators.push_back([=]() {
            if constexpr (direct) {
                rule.head.pred->insert(df::Relation<typename HPred::TupleT>::from_iter(rule.body.pred->var.recent()));
            } else if constexpr (swap) {
                rule.head.pred->insert(df::Relation<typename HPred::TupleT>::from_iter(rule.body.pred->rev_var.recent()));
            }
        });
    }

    template <typename HPred, typename HV1, typename HV2,
              typename LPred, typename LV1, typename LV2,
              typename RPred, typename RV1, typename RV2>
    void add_rule(const Rule<Term<HPred, HV1, HV2>, JoinExpr<Term<LPred, LV1, LV2>, Term<RPred, RV1, RV2>>>& rule) {
        static constexpr bool left_join_v1 = std::is_same_v<LV1, RV1> || std::is_same_v<LV1, RV2>;
        static constexpr bool left_join_v2 = std::is_same_v<LV2, RV1> || std::is_same_v<LV2, RV2>;
        static_assert(left_join_v1 != left_join_v2, "Terms must share exactly one join key");

        using KeyType = std::conditional_t<left_join_v1, LV1, LV2>;
        using LVal    = std::conditional_t<left_join_v1, LV2, LV1>;
        using RVal    = std::conditional_t<std::is_same_v<KeyType, RV1>, RV2, RV1>;

        static constexpr bool head_L_R = std::is_same_v<HV1, LVal> && std::is_same_v<HV2, RVal>;
        static constexpr bool head_R_L = std::is_same_v<HV1, RVal> && std::is_same_v<HV2, LVal>;
        static_assert(head_L_R || head_R_L, "Head variables must match unbound body variables");

        evaluators.push_back([=]() {
            auto& left_input  = left_join_v1 ? rule.body.left.pred->var  : rule.body.left.pred->rev_var;
            auto& right_input = std::is_same_v<KeyType, RV1> ? rule.body.right.pred->var : rule.body.right.pred->rev_var;

            auto kf_left = [](const auto& left_tuple) { return left_tuple.first; };
            auto kf_right = [](const auto& right_tuple) { return right_tuple.first; };

            auto logic = [](const auto& left_tuple, const auto& r_val) {
                if constexpr (head_L_R) return std::pair{left_tuple.second, r_val};
                else return std::pair{r_val, left_tuple.second};
            };

            df::leapjoin_delta(left_input, right_input,
                   kf_left, kf_right,
                   logic, *rule.head.pred);
        });
    }
};

template <typename T1, typename T2>
struct Predicate : IPredicate {
    using TupleT = std::pair<T1, T2>;
    using RevTupleT = std::pair<T2, T1>;

    df::Variable<TupleT> var;
    df::Variable<RevTupleT> rev_var;

    Predicate(Datalog& dl) { dl.register_predicate(this); }

    void insert(const df::Relation<TupleT>& rel) {
        var.insert(rel);
        std::vector<RevTupleT> rev_vec;
        rev_vec.reserve(rel.size());
        for (const auto& p : rel) rev_vec.push_back({p.second, p.first});
        rev_var.insert(df::Relation<RevTupleT>::from_vec(std::move(rev_vec)));
    }

    bool step() override {
        bool c1 = var.changed();
        bool c2 = rev_var.changed();
        return c1 || c2;
    }

    // Generate a Term
    template <typename V1, typename V2>
    auto operator()(V1 v1, V2 v2) {
        return Term<Predicate, V1, V2>{this, v1, v2};
    }

    std::vector<TupleT> extract() {
        df::Relation<TupleT> final_relation = std::move(var).complete();
        return std::move(final_relation.elements);
    }
};
