#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "dartfrog/relation.hpp"
#include "dartfrog/variable.hpp"
#include "datalog/executor.hpp"
#include "datalog/query_planner.hpp"
#include "datalog/var.hpp"

namespace df::datalog {

template <typename V, size_t N> struct ReindexedIndex {
    df::Variable<std::array<V, N>> *base;
    std::array<int, N> perm;
    std::vector<std::vector<std::array<V, N>>> batches;

    template <typename Range>
    void append_permuted(const Range &src, std::vector<std::array<V, N>> &dst) {
        dst.reserve(dst.size() + src.size());
        for (const auto &r : src) {
            std::array<V, N> t{};
            for (size_t k = 0; k < N; k++)
                t[k] = r[perm[k]];
            dst.push_back(t);
        }
    }

    void fold(std::vector<std::array<V, N>> incoming) {
        if (incoming.empty())
            return;
        std::sort(incoming.begin(), incoming.end());
        incoming.erase(std::unique(incoming.begin(), incoming.end()),
                       incoming.end());
        while (!batches.empty() &&
               batches.back().size() <= 8 * incoming.size()) {
            std::vector<std::array<V, N>> last = std::move(batches.back());
            batches.pop_back();
            std::vector<std::array<V, N>> merged;
            merged.reserve(last.size() + incoming.size());
            std::set_union(last.begin(), last.end(), incoming.begin(),
                           incoming.end(), std::back_inserter(merged));
            incoming.swap(merged);
        }
        batches.push_back(std::move(incoming));
    }

    // fold the delta set into the index
    void refresh() {
        std::vector<std::array<V, N>> incoming;
        append_permuted(base->recent_data.elements, incoming);
        fold(std::move(incoming));
    }

    // fold the entire fact set into the index
    void prime() {
        std::vector<std::array<V, N>> incoming;
        for (const auto &batch : base->stable)
            append_permuted(batch.elements, incoming);
        append_permuted(base->recent_data.elements, incoming);
        fold(std::move(incoming));
    }
};

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

    struct RuleAccess {
        std::vector<size_t> inputs;
        std::vector<size_t> outputs;
    };
    std::vector<RuleAccess> rule_access;
    std::vector<size_t> registered_pred_ids;
    size_t worker_count = std::max(1u, std::thread::hardware_concurrency());

    // Type-erased registry of maintained reindexed indexes
    // A given (predicate, order) should only be materialized once
    // and shared across every rule that needs it.
    struct IndexEntry {
        std::unique_ptr<void, void (*)(void *)> storage;
        void (*refresh)(void *);
        void (*prime)(void *);
        const void *batches_ptr;
        const void *base;
    };
    std::vector<IndexEntry> indexes;
    std::map<std::pair<const void *, uint64_t>, size_t> index_registry;

    struct PassGraph {
        std::vector<ThreadExecutor::Task> tasks;
        // input, current pass outputs, next pass outputs
        std::vector<std::array<unsigned char, 3>> changed;
    };

    void refresh_all_indexes() {
        for (auto &e : indexes)
            e.refresh(e.storage.get());
    }

    void prime_all_indexes() {
        for (auto &e : indexes)
            e.prime(e.storage.get());
    }

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

    void build_pass_graph(PassGraph &graph, const std::vector<size_t> &evals) {
        const size_t count = evals.size();
        const size_t width = count + predicates.size();
        graph.tasks.resize(2 * width);
        graph.changed.resize(next_idx);
        auto link = [&](size_t from, size_t to) {
            graph.tasks[from].successors.push_back(to);
            ++graph.tasks[to].prerequisites;
        };
        for (size_t pass = 0; pass < 2; ++pass) {
            const size_t offset = pass * width;
            for (size_t r = 0; r < count; ++r) {
                const size_t rule = evals[r];
                graph.tasks[offset + r].call = [this, &graph, rule, pass] {
                    bool active = false;
                    for (size_t input : rule_access[rule].inputs)
                        active = active || graph.changed[input][pass];
                    if (active)
                        evaluators[rule]();
                };
            }
            for (size_t p = 0; p < predicates.size(); ++p) {
                const size_t id = registered_pred_ids[p];
                const size_t publish = offset + count + p;
                graph.tasks[publish].call = [this, &graph, p, id, pass] {
                    auto &predicate = predicates[p];
                    const bool changed = predicate.step(predicate.instance);
                    graph.changed[id][pass + 1] = changed;
                    if (changed) {
                        predicate.snapshot(predicate.instance);
                        for (auto &index : indexes)
                            if (index.base == predicate.instance)
                                index.refresh(index.storage.get());
                    }
                };
                for (size_t r = 0; r < count; ++r) {
                    const auto &access = rule_access[evals[r]];
                    const bool reads = std::find(
                        access.inputs.begin(), access.inputs.end(), id) !=
                            access.inputs.end();
                    const bool writes = std::find(
                        access.outputs.begin(), access.outputs.end(), id) !=
                            access.outputs.end();
                    if (reads || writes) {
                        link(offset + r, publish);
                        if (pass == 0)
                            link(publish, width + r);
                    }
                }
                if (pass == 0)
                    link(publish, width + count + p);
            }
            if (pass == 0)
                for (size_t r = 0; r < count; ++r)
                    link(r, width + r);
        }
    }

    void run_parallel(ThreadExecutor &executor,
                  const std::vector<size_t> &evals) {
        std::vector<ThreadExecutor::Task> tasks(evals.size());
        for (size_t r = 0; r < evals.size(); ++r)
            tasks[r].call = [this, rule = evals[r]] {
                evaluators[rule].eval_full();
            };
        executor.run(tasks);
    }

  public:
    Datalog() = default;
    explicit Datalog(size_t workers)
        : worker_count(std::max<size_t>(1, workers)) {}

    template <IsPredicate P> void register_predicate(P *p) {
        const size_t id = get_or_add_pred(p);
        if (std::find(
            registered_pred_ids.begin(), registered_pred_ids.end(), id) !=
                registered_pred_ids.end()) {
            return;
        }
        registered_pred_ids.push_back(id);
        predicates.push_back(
            {p, +[](void *x) { return static_cast<P *>(x)->step(); },
             +[](void *x) { static_cast<P *>(x)->snapshot(); }});
    }

    // Return the maintained reindexed index for (base, Perm...), creating it on
    // first request.
    template <int... Perm, typename V, size_t N>
    const std::vector<std::vector<std::array<V, N>>> *
    get_or_make_index(df::Variable<std::array<V, N>> *base,
                      const void *owner = nullptr) {
        static_assert(sizeof...(Perm) == N,
                      "permutation width must equal the predicate arity");
        uint64_t code = N;
        ((code =
              (code << 4) | static_cast<uint64_t>(static_cast<unsigned>(Perm))),
         ...);
        const auto key = std::make_pair(static_cast<const void *>(base), code);
        if (auto it = index_registry.find(key); it != index_registry.end()) {
            if (owner != nullptr)
                indexes[it->second].base = owner;
            return static_cast<
                const std::vector<std::vector<std::array<V, N>>> *>(
                indexes[it->second].batches_ptr);
        }
        auto *ri = new ReindexedIndex<V, N>{base, {Perm...}, {}};
        indexes.push_back(IndexEntry{
            {ri,
             +[](void *p) { delete static_cast<ReindexedIndex<V, N> *>(p); }},
            +[](void *p) { static_cast<ReindexedIndex<V, N> *>(p)->refresh(); },
            +[](void *p) { static_cast<ReindexedIndex<V, N> *>(p)->prime(); },
            &ri->batches, owner});
        index_registry[key] = indexes.size() - 1;
        return &ri->batches;
    }

    template <typename Planner> void bind_indexes(Planner &p) {
        constexpr size_t NA = Planner::NumAtoms;
        for_indices<NA>([&]<size_t S>() {
            for_indices<NA>([&]<size_t I>() {
                if constexpr (I != S) {
                    constexpr auto perm =
                        Planner::template atom_reindex_perm<S, I>();
                    if constexpr (!is_identity_perm(perm)) {
                        auto *base = std::get<I>(p.atoms).pred;
                        p.index_batches[S][I] =
                            [&]<size_t... Ks>(
                                std::index_sequence<Ks...>) -> const void * {
                            return get_or_make_index<perm[Ks]...>(&base->var, base);
                        }(std::make_index_sequence<perm.size()>{});
                    }
                }
            });
        });
    }

    template <typename V> void make_symmetric(Predicate<V, 2> &pred);

    template <int... Cols, typename V, size_t N>
        requires(sizeof...(Cols) == N)
    void make_reindexed(Predicate<V, N> &source, Predicate<V, N> &target);

    template <typename HeadPred, typename... HeadVars, typename BodyPred,
              typename... BodyVars>
    void add_rule(const Rule<Term<HeadPred, HeadVars...>,
                             Term<BodyPred, BodyVars...>> &rule) {
        if constexpr (sizeof...(HeadVars) == sizeof...(BodyVars)) {
            if constexpr ((std::is_same_v<HeadVars, BodyVars> && ...)) {
                size_t head_idx = get_or_add_pred(rule.head.pred);
                const size_t body_idx = get_or_add_pred(rule.body.pred);
                add_dep(body_idx, head_idx, false);

                rule_access.push_back({{body_idx}, {head_idx}});
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
        add_rule(Rule{rule.head, body_as_conjunction(rule.body)});
    }

    template <typename Head, typename Body>
    void add_rule(const Rule<Head, Body> &rule) {
        auto heads = heads_as_tuple(rule.head);
        const auto &body = body_as_conjunction(rule.body);
        using Atoms = std::remove_cvref_t<decltype(body.pos)>;
        using Filters = std::remove_cvref_t<decltype(body.filt)>;
        constexpr size_t H = std::tuple_size_v<decltype(heads)>;
        const std::array<size_t, H> head_idxs =
            [&]<size_t... Hs>(std::index_sequence<Hs...>) {
                return std::array<size_t, H>{
                    get_or_add_pred(std::get<Hs>(heads).pred)...};
            }(std::make_index_sequence<H>{});
        for (size_t hi : head_idxs) {
            register_pos_deps(
                body.pos, hi,
                std::make_index_sequence<std::tuple_size_v<Atoms>>{});
            register_neg_deps<Filters>(body.filt, hi);
        }
        // Tie every head to head_idxs[0]'s stratum with zero-weight edges.
        for (size_t k = 1; k < H; k++) {
            add_dep(head_idxs[0], head_idxs[k], false);
            add_dep(head_idxs[k], head_idxs[0], false);
        }

        RuleAccess access{{}, {head_idxs.begin(), head_idxs.end()}};
        for_indices<std::tuple_size_v<Atoms>>([&]<size_t I>() {
            access.inputs.push_back(get_or_add_pred(std::get<I>(body.pos).pred));
        });
        for_indices<std::tuple_size_v<Filters>>([&]<size_t F>() {
            if constexpr (is_negated<std::tuple_element_t<F, Filters>>::value)
                access.inputs.push_back(
                    get_or_add_pred(std::get<F>(body.filt).pred));
        });
        rule_access.push_back(std::move(access));

        eval_head_idx.push_back(head_idxs[0]);
        QueryPlanner<decltype(heads), Atoms, Filters> runner{
            std::move(heads), body.pos, body.filt};
        bind_indexes(runner);
        evaluators.push_back(make_eval(std::move(runner)));
    }

    void solve() {
        const auto stratum_evals = compute_strata();
        ThreadExecutor executor(
            std::min(worker_count, std::max<size_t>(1, evaluators.size())));

        prime_all_indexes();

        for (const auto &evals : stratum_evals) {
            for (auto &h : predicates)
                h.step(h.instance);

            for (auto &h : predicates)
                h.snapshot(h.instance);

            refresh_all_indexes();
            run_parallel(executor, evals);
            solve_stratum(executor, evals);
        }
    }

  private:
    void solve_stratum(ThreadExecutor &executor,
                       const std::vector<size_t> &evals) {
        PassGraph graph;
        build_pass_graph(graph, evals);
        bool changed = false;
        for (size_t p = 0; p < predicates.size(); ++p) {
            auto &predicate = predicates[p];
            const bool advanced = predicate.step(predicate.instance);
            graph.changed[registered_pred_ids[p]][0] = advanced;
            changed = changed || advanced;
            predicate.snapshot(predicate.instance);
        }
        refresh_all_indexes();
        while (changed) {
            executor.run(graph.tasks);
            changed = false;

            // Move the accumulated outputs from the next round
            // down into the current round
            for (auto &state : graph.changed) {
                state[0] = state[2];
                state[1] = state[2] = 0;
                changed = changed || state[0];
            }
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
    mutable std::shared_ptr<std::mutex> insertion_guard =
        std::make_shared<std::mutex>();

    // Provide a default constructor so Predicate can be used as a free query
    // body
    Predicate() = default;
    Predicate(Datalog &dl) { dl.register_predicate(this); }

    void insert(df::Relation<TupleT> rel) {
        if (rel.empty())
            return;
        std::lock_guard lock(*insertion_guard);
        var.to_add.push_back(std::move(rel));
    }

    // Promote the new facts from the last iteration
    // into var.stable
    void snapshot() {
        if (var.recent_data.empty())
            return;
        df::Relation<TupleT> incoming{
            std::vector<TupleT>(var.recent_data.elements)};
        while (!var.stable.empty() &&
               var.stable.back().size() <= 8 * incoming.size()) {
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
        std::lock_guard lock(*insertion_guard);
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
    add_rule(pred(y, x) %= pred(x, y));
}

template <int... Cols, typename V, size_t N>
    requires(sizeof...(Cols) == N)
void Datalog::make_reindexed(Predicate<V, N> &source, Predicate<V, N> &target) {
    [&]<size_t... Js>(std::index_sequence<Js...>) {
        add_rule(target(Var<Cols>{}...) %= source(Var<(int)Js>{}...));
    }(std::make_index_sequence<N>{});
}

// Quick convenience wrappers to allow constant literals
// without boilerplate
template <typename V, size_t N>
auto &Const(std::vector<std::array<V, N>> data) {
    using Entry = std::pair<std::vector<std::array<V, N>>, Predicate<V, N>>;
    static std::vector<std::unique_ptr<Entry>> cache;

    for (auto &entry : cache) {
        if (entry->first == data) {
            return entry->second;
        }
    }

    auto &new_entry =
        *cache.emplace_back(std::make_unique<Entry>(data, Predicate<V, N>{}));

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
