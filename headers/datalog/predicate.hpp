#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <span>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "dartfrog/relation.hpp"
#include "dartfrog/variable.hpp"
#include "datalog/query_planner.hpp"
#include "datalog/var.hpp"

namespace df::datalog {

template <typename P>
concept IsPredicate = requires(P &p) {
    { p.step() } -> std::same_as<bool>;
    { p.snapshot() } -> std::same_as<void>;
};

struct PredHandle {
    void *instance;
    bool (*step)(void *);
    void (*snapshot)(void *);
};

struct EvalHandle {
    std::unique_ptr<void, void (*)(void *)> storage;
    void (*call_delta)(void *);
    void (*call_full)(void *);
    void operator()() const { call_delta(storage.get()); }
    void eval_full() const { call_full(storage.get()); }
};

template <typename F> EvalHandle make_eval(F f) {
    auto *p = new F(std::move(f));
    return {{p, +[](void *x) { delete static_cast<F *>(x); }},
            +[](void *x) { (*static_cast<F *>(x))(); },
            +[](void *x) { static_cast<F *>(x)->eval_full(); }};
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
            ++next_idx;
            dep_edges.emplace_back();
        }
        return it->second;
    }

    void add_dep(size_t from, size_t to, bool negative) {
        dep_edges[from].push_back({to, negative});
    }

    template <typename Atoms, size_t... Is>
    void register_pos_deps(const Atoms &atoms, size_t head_idx,
                           std::index_sequence<Is...>) {
        (add_dep(get_or_add_pred(std::get<Is>(atoms).pred), head_idx, false),
         ...);
    }

    template <typename Filters>
    void register_neg_deps(const Filters &filters, size_t head_idx) {
        for_indices<std::tuple_size_v<Filters>>([&]<size_t F>() {
            using Filt = std::tuple_element_t<F, Filters>;
            if constexpr (is_negated<Filt>::value)
                add_dep(get_or_add_pred(std::get<F>(filters).pred), head_idx,
                        true);
        });
    }

    std::vector<std::vector<size_t>> compute_strata() const {
        size_t N = next_idx;
        std::vector<int> strata(N, 0);
        for (size_t round = 0; round < N + 1; round++) {
            bool changed = false;
            for (size_t from = 0; from < N; from++) {
                for (const auto &edge : dep_edges[from]) {
                    int new_stratum = strata[from] + (edge.negative ? 1 : 0);
                    if (new_stratum > strata[edge.to]) {
                        strata[edge.to] = new_stratum;
                        changed = true;
                    }
                }
            }
            if (!changed)
                break;
            // Bellman-ford guaranteed to terminate within N rounds, so reaching
            // this means the program has a dependency cycle somewhere
            if (round == N)
                throw std::logic_error(
                    "Datalog program is not stratifiable: "
                    "negation cycle detected in predicate dependency graph.");
        }
        int max_s = strata.empty()
                        ? 0
                        : *std::max_element(strata.begin(), strata.end());
        std::vector<std::vector<size_t>> result(max_s + 1);
        for (size_t i = 0; i < evaluators.size(); i++) {
            size_t head_pred_idx = eval_head_idx[i];
            result[head_pred_idx < N ? strata[head_pred_idx] : 0].push_back(i);
        }
        return result;
    }

  public:
    template <IsPredicate P> void register_predicate(P *p) {
        predicates.push_back(
            {p, +[](void *x) { return static_cast<P *>(x)->step(); },
             +[](void *x) { static_cast<P *>(x)->snapshot(); }});
    }

    template <typename V> void make_symmetric(Predicate<V, 2> &pred);

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
                    void eval_full() const {
                        for (const auto &batch : body->var.stable)
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
        register_pos_deps(rule.body.pos, head_idx,
                          std::make_index_sequence<std::tuple_size_v<Atoms>>{});
        register_neg_deps<Filters>(rule.body.filt, head_idx);
        eval_head_idx.push_back(head_idx);
        QueryPlanner<Term<HeadPred, HeadVars...>, Atoms, Filters> runner{
            rule.head, rule.body.pos, rule.body.filt};

        // Store the query as a type-erased callback so the rest of the engine
        // can treat them uniformly
        evaluators.push_back(make_eval(std::move(runner)));
    }

    void solve() {
        auto stratum_evals = compute_strata();
        for (const auto &evals : stratum_evals) {
            for (auto &h : predicates)
                h.step(h.instance);
            for (auto &h : predicates)
                h.snapshot(h.instance);
            for (size_t i : evals)
                evaluators[i].eval_full();
            solve_stratum(evals);
        }
    }

  private:
    void solve_stratum(const std::vector<size_t> &evals) {
        while (true) {
            bool changed = false;
            for (auto &h : predicates)
                if (h.step(h.instance))
                    changed = true;
            if (!changed)
                break;
            for (auto &h : predicates)
                h.snapshot(h.instance);
            for (size_t i : evals)
                evaluators[i]();
        }
    }

  public:
};

template <typename V, size_t N> struct Predicate {
    using TupleT = std::array<V, N>;

    // var.stable holds the current facts
    // var.recent_data holds new facts discovered in the last iteration
    // var.to_add holds newly produced facts during the iteration
    df::Variable<TupleT> var;

    // Provide a default constructor so Predicate can be used as a free query
    // body
    Predicate() = default;
    Predicate(Datalog &dl) { dl.register_predicate(this); }

    void insert(const df::Relation<TupleT> &rel) { var.insert(rel); }

    // Promote the new facts from the last iteration
    // into var.stable
    void snapshot() {
        if (var.recent_data.empty())
            return;
        df::Relation<TupleT> incoming{
            std::vector<TupleT>(var.recent_data.elements)};
        while (!var.stable.empty() &&
               var.stable.back().size() <= 2 * incoming.size()) {
            auto last = std::move(var.stable.back());
            var.stable.pop_back();
            incoming = std::move(incoming).merge(std::move(last));
        }
        var.stable.push_back(std::move(incoming));
    }

    bool stable_contains(const TupleT &t) const {
        for (const auto &batch : var.stable)
            if (batch.binary_search(t).has_value())
                return true;
        return false;
    }

    // Deduplicate the newly produced facts and promote them
    // into to recent_data
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

    template <int... Ids> auto operator()(Var<Ids>...) {
        static_assert(sizeof...(Ids) == N,
                      "wrong number of variables for this predicate's arity");
        return Term<Predicate, Var<Ids>...>{this};
    }

    // Non-destructively return the result facts
    std::vector<TupleT> peek() const {
        std::vector<TupleT> result;
        var.for_each_stable_set([&](std::span<const TupleT> batch) {
            result.insert(result.end(), batch.begin(), batch.end());
        });
        std::sort(result.begin(), result.end());
        return result;
    }

    // Destructively return the result facts
    std::vector<TupleT> extract() { return std::move(var).complete().elements; }
};

// Convert a directed relation into an undirected relation
// by generating the inverse edge
template <typename V> void Datalog::make_symmetric(Predicate<V, 2> &pred) {
    Var<0> x;
    Var<1> y;
    add_rule(pred(y, x) <<= pred(x, y));
}

// Quick convenience wrappers to allow constant literals
// without boilerplate
template <typename V, size_t N>
auto &Const(std::vector<std::array<V, N>> data) {
    static std::vector<
        std::pair<std::vector<std::array<V, N>>, Predicate<V, N>>>
        cache;

    for (auto &entry : cache) {
        if (entry.first == data) {
            return entry.second;
        }
    }

    auto &new_entry = cache.emplace_back(data, Predicate<V, N>{});

    new_entry.second.insert(
        df::Relation<std::array<V, N>>::from_vec(std::move(data)));
    new_entry.second.commit();

    return new_entry.second;
}

template <typename V> auto &Const(std::initializer_list<V> values) {
    std::vector<std::array<V, 1>> data;
    data.reserve(values.size());

    for (const auto &v : values) {
        data.push_back({v});
    }

    return Const<V, 1>(std::move(data));
}

} // namespace df::datalog
