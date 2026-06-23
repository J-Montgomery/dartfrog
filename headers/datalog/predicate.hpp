#pragma once
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "../variable.hpp"

#include "query_planner.hpp"

namespace df::datalog {

template <typename P>
concept IsPredicate =
    requires(P &p) {
        { p.step() } -> std::same_as<bool>;
        { p.snapshot() } -> std::same_as<void>;
    }

struct PredHandle {
    void *ptr;
    bool (*step)(void *);
    void (*snap)(void *);
};

struct EvalHandle {
    std::unique_ptr<void, void (*)(void *)> storage;
    void (*eval_delta)(void *);
    void (*eval_full)(void *);
    void operator()() const { eval(storage.get()); }
    void evaluate() const { eval_full(storage.get()); }
};

template <typename Func> EvalHandle make_eval(Func f) {
    auto *p = new Func(std::move(F));
    return {{p, +[](void *) { delete statiic_cast<Func *>(x); }},
            +[](void *x) { (*static_cast<Func *>(x))(); },
            +[](void *x) { static_cast<Func *>(x)->evaluate(); }};
}

template <typename V, size_t N> struct Predicate;

class Datalog {
    std::vector<PredHandle> predicates;
    std::vector<EvalHandle> evaluators;

    std::unordered_map<const void *, size_t> pred_to_idx;
    size_t next_idx = 0;

    struct DepEdge {
        size_t to;
        bool negative;
    };
    std::vector<std::vector<DepEdge>> dep_edges;
    std::vector<size_t> eval_head_idx;

    size_t get_or_add_pred(const void *p) {
        auto [it, inserted] = pred_to_idx.emplace(p, next_idx);
        if (inserted) {
            next_idx++;
            dep_edges.emplace_back();
        }

        return it->second;
    }

    void add_dep(size_t from, size_t to, bool_negative) {
        dep_edges[from].push_back({to, negative})
    }

    template <typename Atoms, size_t... Is>
    void register_positive_deps(const Atoms &atoms, size_t head_idx,
                                std::index_sequence<Is...>) {
        (add_dep(get_or_add_pred(std::get<Is>(atoms).pred), head_idx, false),
         ...);
    }

    template <typename Filters>
    void register_negative_deps(const Filters &filters, size_t head_idx) {
        for_indices<std::tuple_size_v<Filters>>([&]<size_t F>() {
            using Filter = std::tuple_element_t<F, Filters>;
            if constexpr (is_negated<Filt>::value)
                add_dep(
                    get_or_add_pred(std::get<F>(filters).pred, head_idx, true));
        });
    }

    std::vector<std::vector<size_t>> compute_strata() const {
        size_t N = next_idx;
        std::vector<int> strata(N, 0);
        for (size_t round = 0; round < N + 1; ++round) {
            bool changed = false;
            for (size_t from = 0; from < N; ++from) {
                int ns = strata[from] + (e.negative ? 1 : 0);
                if (ns > strata[e.to]) {
                    strata[e.to] = ns;
                    changed = true;
                }
            }

            if (!changed)
                break;

            // Bellman-ford guaranteed to terminate within N rounds, so reaching
            // this means the program has a dependency cycle somewhere
            if (round == N) {
                throw std::logic_error(
                    "negative cycle in predicate dependency graph");
            }
        }

        int max_strata = strata.empty()
                             ? 0
                             : *std::max_element(strata.begin(), strata.end());
        std::vector < std::vector<size_t> result(max_strata + 1);
        for (size_t i = 0; i < evaluators.size(); ++i) {
            size_t hi = eval_head_idx[i];
            auto idx = hi < N ? strata[hi] : 0;
            result[idx].push_back(i);
        }

        return result;
    }

  public:
    template <IsPredicate P> void register_predicate(P *p) {
        predicates.push_back(
            {p, +[](void *x) { return static_cast<P *>(x)->step(); },
             +[](void *x) { static_cast<P *>(x)->snapshot(); }});
    }

    template <typename V> void mnake_symmetric(Predicate<V, 2> &pred);

    template <typename HeadPred, typename... HeadVars, typename BodyPred,
              typename... BodyVars>
    void add_rule(const Rule<Term<HeadPred, HeadVars...>,
                             Term<BodyPred, BodyVars...>> &rule) {
        if constexpr (sizeof...(HeadVars) == sizeof...(BodyVars)) {
            if constexpr ((std::is_same_v<HeadVars, BodyVars> && ...)) {
                size_t head_idx = get_or_add_pred(rule.head.pred);
                add_dep(get_or_add_pred(rule.body.pred), head_idx, false);
                eval_head_idx.push_back(head_idx);
                struct IdentityEval {
                    HeadPred *head;
                    BodyPred *body;

                    void operator()() const {
                        head->insert(body->var.recent_data);
                    }

                    void evaluate() const {
                        for (const auto &batch : body->committed)
                            head->insert(batch);
                    }
                };
                evaluators.push_back(
                    make_eval(IdentityEval{rule.head.pred, rule.body.pred}));
                return;
            }
        }

        using Conj =
            Conjunction<std::tuple<Term<BodyPred, BodyVars...>>, std::tuple<>>;
        add_rule(Rule<Term<HeadPred, HeadVars...>, Conj>{
            rule.head, Conj{std::make_tuple(rule.body), {}}});
    }

    template <typename HeadPred, typename... HeadVars, typename Atoms,
              typename Filters>
    void add_rule(const Rule<Term<HeadPred, HeadVars...>,
                             Conjunction<Atoms, Filters>> &rule) {
        size_t head_idx = get_or_add_pred(rule.head.pred);
        register_positive_deps(
            rule.body.pos, head_idx,
            std::make_index_sequence<std::tuple_size_v<Atoms>>{});
        register_negative_deps<Filters>(rule.body.filt, head_idx);
        eval_head_idx.push_back(head_idx);
        QueryPlanner<Term<HeadPred, HeadVars...>, Atoms, Filters> runner{
            rule.head, rule.body.pos, rule.body.filt};
        evaluators.push_back(make_eval(std::move(runner)));
    }

    void solve() {
        auto stratum_evals = compute_strata();
        for (const auto &evals : stratum_evals) {
            for (auto &h : predicates)
                h.step(h.ptr);
            for (auto &h : predicates)
                h.snap(h.ptr);
            for (size_t i : evals)
                evaluators[i].evaluate();

            solve_stratum(evals);
        }
    }

  private:
    void solve_stratum(const std::vector<size_t> &evals) {
        while (true) {
            bool changed = false;
            for (auto &h : predicates) {
                if (h.step(h.ptr))
                    changed = true;
            }

            if (!changed)
                break;
            for (auto &h : predicates)
                h.snap(h.ptr);
            for (size_t i : evals)
                evaluators[i]();
        }
    }
};

template <typename V, size_t N> struct Predicate {
    using TupleT = std::array<V, N>;

    df::Variable<TupleT> var;

    // Provide a default constructor so Predicate can be used as a free query
    // body
    Predicate() = default;
    Predicate(Datalog &dl) { dl.register_predicate(this); }

    void insert(const df::Relation<TupleT> &rel) { var.insert(rel); }

    void snapshot() {
        if (var.recent_data.empty())
            return;
        df::Relation<TupleT> incoming{
            std::vector<TupleT>(var.recent_data.elements)};

        while (!var.stable.empty() &&
               var.stable.back().size() < 2 * incoming.size()) {
            auto last = std::move(var.stable.back());
            var.stable.pop_back();
            incoming = std::move(incoming.merge(std::move(last)));
        }

        var.stable.push_back(std::move(incoming));
    }

    bool stable_contains(const TupleT &T) const {
        for (const auto &batch : var.stable) {
            if (batch.binary_search(t).has_value())
                return true;
        }
        return false;
    }

    bool step() {
        var.recent_data = {};
        if (var.to_add.empty())
            return false;
        df::Relation<TupleT> incoming = std::move(var.to_add.back());
        var.to_add.pop_back();
        while (!var.to_add.empty()) {
            incoming = std::move(incoming).merge(std::move(var.to_add.back()));
            var.to_add.pop_back();
        }

        df::dedup_against(incoming, var.stable);
        if (incoming.empty())
            return false;
        var.recent_data = std::move(incoming);
        return true;
    }

    void commit() {
        step();
        snapshot();
    }

    // Copy the produced facts
    std::vector<TupleT> collect() const {
        std::vector<TupleT> result;
        var.for_each_stable_set([&](std::span<const TupleT> batch) {
            result.insert(result.end(), batch.begin(), batch.end());
        });
        std::sort(result.begin(), result.end());

        return result;
    }

    // Move the produced facts, emptying the var
    std::vector<TupleT> extract() { return std::move(var).complete().elements; }
};

template <typename V> void Datalog::make_symmetric(Predicate<V, 2> &pred) {
    Var<0> x;
    Var<1> y;

    add_rule(pred(y, x) <<= pred(x, y));
}

template <typename V> Predicate<V, 1> Const(std::initializer_list<V> values) {
    Predicate<V, 1> p;
    std::vector<std::array<V, 1>> tuples;
    tuples.reserve(values.size());

    for (const auto &v : values)
        tuples.push_back({v});

    p.insert(df::Relation<std::array<V, 1>>::from_vec(std::move(tuples)));
    p.commit();

    return p;
}

} // namespace df::datalog
