#pragma once

#include <algorithm>
#include <array>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "dartfrog/relation.hpp"
#include "datalog/lftj.hpp"
#include "datalog/var.hpp"

namespace df::datalog {

static constexpr size_t MAX_ARITY = 8;

template <typename T> struct atom_traits;
template <typename P, int... Ids> struct atom_traits<Term<P, Var<Ids>...>> {
    using pred_t = P;
    static constexpr size_t arity = sizeof...(Ids);
    static constexpr std::array<int, sizeof...(Ids)> var_ids = {Ids...};
};

template <typename Atoms> constexpr auto atom_ids() {
    constexpr size_t NumAtoms = std::tuple_size_v<Atoms>;
    std::array<std::array<int, MAX_ARITY>, NumAtoms> result{};
    for (auto &row : result)
        row.fill(-1);
    for_indices<NumAtoms>([&]<size_t I>() {
        using AT = atom_traits<std::tuple_element_t<I, Atoms>>;
        for (size_t j = 0; j < AT::arity; j++)
            result[I][j] = AT::var_ids[j];
    });
    return result;
}

template <typename Atoms> constexpr auto atom_arities() {
    constexpr size_t NumAtoms = std::tuple_size_v<Atoms>;
    std::array<size_t, NumAtoms> result{};
    for_indices<NumAtoms>([&]<size_t I>() {
        result[I] = atom_traits<std::tuple_element_t<I, Atoms>>::arity;
    });
    return result;
}

template <typename Atoms> constexpr size_t num_vars() {
    constexpr auto ids = atom_ids<Atoms>();
    constexpr auto arities = atom_arities<Atoms>();
    int max_var_id = 0;
    for (size_t atom = 0; atom < ids.size(); atom++)
        for (size_t col = 0; col < arities[atom]; col++)
            if (ids[atom][col] > max_var_id)
                max_var_id = ids[atom][col];
    for (int var_id = 0; var_id <= max_var_id; var_id++) {
        bool found = false;
        for (size_t atom = 0; atom < ids.size() && !found; atom++)
            for (size_t col = 0; col < arities[atom] && !found; col++)
                if (ids[atom][col] == var_id)
                    found = true;
        if (!found)
            throw std::logic_error("variable IDs must be contiguous from 0");
    }
    return (size_t)(max_var_id + 1);
}

template <size_t NumVars>
constexpr std::array<int, NumVars>
invert(const std::array<int, NumVars> &order) {
    std::array<int, NumVars> result{};
    for (size_t i = 0; i < NumVars; i++)
        result[order[i]] = (int)i;
    return result;
}

template <size_t A>
constexpr bool is_identity_perm(const std::array<int, A> &perm) {
    for (size_t i = 0; i < A; i++)
        if (perm[i] != static_cast<int>(i))
            return false;
    return true;
}

template <size_t N>
constexpr bool is_identity_prefix(const std::array<int, N> &positions) {
    for (size_t i = 0; i < N; i++)
        if (positions[i] != static_cast<int>(i))
            return false;
    return true;
}

// make_order determines the variable binding order
// by greedily assuming unbound variables that share
// rules with lots of bound variables should be
// bound and proposed before less common variables
template <typename Atoms> constexpr auto make_order(size_t s) {
    constexpr size_t NumVars = num_vars<Atoms>();
    constexpr size_t NumAtoms = std::tuple_size_v<Atoms>;
    constexpr auto ids = atom_ids<Atoms>();
    constexpr auto arities = atom_arities<Atoms>();

    std::array<int, NumVars> order{};
    std::array<bool, NumVars> bound{};
    std::array<int, NumVars> score{};

    bound.fill(false);
    score.fill(0);

    auto bind = [&](int var_id) {
        bound[var_id] = true;
        for (size_t atom = 0; atom < NumAtoms; atom++) {
            bool has_var = false;
            for (size_t col = 0; col < arities[atom]; col++)
                if (ids[atom][col] == var_id) {
                    has_var = true;
                    break;
                }
            if (!has_var)
                continue;
            for (size_t col = 0; col < arities[atom]; col++) {
                int neighbor = ids[atom][col];
                if (neighbor >= 0 && !bound[neighbor])
                    ++score[neighbor];
            }
        }
    };

    // Bind the bound variables first
    size_t num_bound = 0;
    for (size_t col = 0; col < arities[s]; col++) {
        order[num_bound++] = ids[s][col];
        bind(ids[s][col]);
    }

    // Now bind the unbound variables in order of the scores
    // we've calculated
    while (num_bound < NumVars) {
        int best_var = -1, best_score = -1;
        for (int var_id = 0; var_id < (int)NumVars; var_id++)
            if (!bound[var_id] && score[var_id] > best_score) {
                best_score = score[var_id];
                best_var = var_id;
            }
        order[num_bound++] = best_var;
        bind(best_var);
    }
    return order;
}

/* Negation */
template <typename T> struct is_negated : std::false_type {};
template <typename P, typename... Vars>
struct is_negated<NegatedTerm<P, Vars...>> : std::true_type {};

template <typename T> struct negated_var_ids;
template <typename P, int... Ids>
struct negated_var_ids<NegatedTerm<P, Var<Ids>...>> {
    static constexpr size_t arity = sizeof...(Ids);
    static constexpr std::array<int, sizeof...(Ids)> value = {Ids...};
};

/* Expression filters */

template <typename T> struct is_expression_filter : std::false_type {};
template <typename Func, int... VarIds>
struct is_expression_filter<ExpressionFilter<Func, VarIds...>>
    : std::true_type {};

template <typename T> struct expression_filter_var_ids_impl;
template <typename Func, int... VarIds>
struct expression_filter_var_ids_impl<ExpressionFilter<Func, VarIds...>> {
    static constexpr std::array<int, sizeof...(VarIds)> value = {VarIds...};
};

/* Aggregates */

template <typename T> struct is_aggregate_filter : std::false_type {};

template <typename Func, int OutVarId, int ValueVarId, int... GroupVarIds>
struct is_aggregate_filter<
    AggregateFilter<Func, OutVarId, ValueVarId, GroupVarIds...>>
    : std::true_type {};

template <typename Func, int OutVarId, int... GroupVarIds>
struct is_aggregate_filter<AggregateRowsFilter<Func, OutVarId, GroupVarIds...>>
    : std::true_type {};

template <typename T>
inline constexpr bool is_aggregate_filter_v = is_aggregate_filter<T>::value;

template <typename T> struct aggregate_traits;

template <typename Func, int OutVarId, int ValueVarId, int... GroupVarIds>
struct aggregate_traits<
    AggregateFilter<Func, OutVarId, ValueVarId, GroupVarIds...>> {
    using func_t = Func;

    static constexpr int out_var_id = OutVarId;
    static constexpr int value_var_id = ValueVarId;

    static constexpr std::array<int, sizeof...(GroupVarIds)> group_var_ids = {
        GroupVarIds...};
};

template <typename Func, int OutVarId, int... GroupVarIds>
struct aggregate_traits<AggregateRowsFilter<Func, OutVarId, GroupVarIds...>> {
    using func_t = Func;

    static constexpr int out_var_id = OutVarId;
    static constexpr int value_var_id = -1;

    static constexpr std::array<int, sizeof...(GroupVarIds)> group_var_ids = {
        GroupVarIds...};
};

template <typename Filters> constexpr size_t aggregate_count() {
    return []<typename... Ts>(std::type_identity<std::tuple<Ts...>>) {
        return (0 + ... + (is_aggregate_filter_v<Ts> ? 1 : 0));
    }(std::type_identity<Filters>{});
}

template <typename Filters> constexpr size_t aggregate_index() {
    size_t result = std::tuple_size_v<Filters>;

    for_indices<std::tuple_size_v<Filters>>([&]<size_t I>() {
        using Filt = std::tuple_element_t<I, Filters>;
        if constexpr (is_aggregate_filter_v<Filt>)
            result = I;
    });

    return result;
}

template <typename Agg> constexpr bool aggregate_group_ids_unique() {
    constexpr auto ids = aggregate_traits<Agg>::group_var_ids;

    for (size_t i = 0; i < ids.size(); i++) {
        for (size_t j = i + 1; j < ids.size(); j++) {
            if (ids[i] == ids[j])
                return false;
        }
    }
    return true;
}

// A quick combiner that works for any integral T
template <typename T> constexpr T permute(T x) noexcept {
    T b = ((x * x) | static_cast<T>(0x5)) + x;
    return b ^ (x << 1);
}

template <typename V, size_t N> struct AggregateArrayHash {
    size_t operator()(const std::array<V, N> &key) const
        noexcept(noexcept(std::hash<V>{}(std::declval<const V &>()))) {
        size_t seed = 0;

        for (const auto &elem : key) {
            size_t h = std::hash<V>{}(elem);
            seed = permute(seed ^ h);
        }

        return seed;
    }
};

/* Relational operators */

template <typename T> struct filter_vars;
template <Cmp Op, int A, int B>
struct filter_vars<Compare<Op, Var<A>, Var<B>>> {
    static constexpr int a_id = A;
    static constexpr int b_id = B;
    static constexpr Cmp op = Op;
};

template <typename T> struct filter_var_ids;
template <typename Func, int... VarIds>
struct filter_var_ids<ExpressionFilter<Func, VarIds...>> :
    expression_filter_var_ids_impl<ExpressionFilter<Func, VarIds...>> {};
template <typename P, int... Ids>
    struct filter_var_ids<NegatedTerm<P, Var<Ids>...>> :
    negated_var_ids<NegatedTerm<P, Var<Ids>...>> {};
template <Cmp Op, int A, int B>
struct filter_var_ids<Compare<Op, Var<A>, Var<B>>> {
    static constexpr std::array<int, 2> value = {A, B};
};

template <Cmp op, typename T> constexpr bool cmp_apply(const T &x, const T &y) {
    if constexpr (op == Cmp::Lt)
        return x < y;
    else if constexpr (op == Cmp::Le)
        return x <= y;
    else if constexpr (op == Cmp::Gt)
        return x > y;
    else if constexpr (op == Cmp::Ge)
        return x >= y;
    else if constexpr (op == Cmp::Ne)
        return x != y;
    else
        return x == y;
}

template <typename Atoms> constexpr bool has_duplicate_var_atom() {
    constexpr size_t NumAtoms = std::tuple_size_v<Atoms>;
    constexpr auto ids = atom_ids<Atoms>();
    constexpr auto arities = atom_arities<Atoms>();
    for (size_t atom = 0; atom < NumAtoms; atom++)
        for (size_t i = 0; i < arities[atom]; i++)
            for (size_t j = i + 1; j < arities[atom]; j++)
                if (ids[atom][i] == ids[atom][j])
                    return true;
    return false;
}

// True iff Head references only variable ids in [0, NumVars).
template <typename Head, size_t NumVars> constexpr bool one_head_covered() {
    for (int id : atom_traits<Head>::var_ids)
        if (id < 0 || id >= static_cast<int>(NumVars))
            return false;
    return true;
}

// True iff every head in HeadTup references only variable ids in [0, NumVars).
template <typename HeadTup, size_t NumVars, size_t... Hs>
constexpr bool all_heads_covered(std::index_sequence<Hs...>) {
    return (one_head_covered<std::tuple_element_t<Hs, HeadTup>, NumVars>() &&
            ...);
}

template <typename Head, typename Agg>
constexpr bool one_aggregate_head_covered() {
    for (int id : atom_traits<Head>::var_ids) {
        if (id < 0)
            return false;

        if (id == aggregate_traits<Agg>::out_var_id)
            continue;

        bool is_group = false;
        for (int group_id : aggregate_traits<Agg>::group_var_ids) {
            if (id == group_id) {
                is_group = true;
                break;
            }
        }

        if (!is_group)
            return false;
    }

    return true;
}

template <typename HeadTup, typename Agg, size_t... Hs>
constexpr bool all_aggregate_heads_covered(std::index_sequence<Hs...>) {
    return (
        one_aggregate_head_covered<std::tuple_element_t<Hs, HeadTup>, Agg>() &&
        ...);
}

template <typename HeadTuple, typename Atoms, typename Filters>
struct QueryPlanner {
    HeadTuple heads;
    Atoms atoms;
    Filters filters;

    std::array<std::array<const void *, std::tuple_size_v<Atoms>>,
               std::tuple_size_v<Atoms>>
        index_batches{};

    using FirstAtom = std::tuple_element_t<0, Atoms>;
    using FirstPred = typename atom_traits<FirstAtom>::pred_t;
    using V = typename FirstPred::TupleT::value_type;

    static constexpr size_t NumVars = num_vars<Atoms>();
    static constexpr size_t NumAtoms = std::tuple_size_v<Atoms>;
    static constexpr size_t NumHeads = std::tuple_size_v<HeadTuple>;

    template <size_t H> using HeadAt = std::tuple_element_t<H, HeadTuple>;

    static constexpr size_t NumAggregates = aggregate_count<Filters>();
    static constexpr bool HasAggregate = NumAggregates != 0;
    static constexpr size_t AggregateIndex = aggregate_index<Filters>();

    static_assert(NumAggregates <= 1, "A rule can have at most 1 aggregate");

    template <bool Enabled, typename Dummy = void>
    struct aggregate_type_holder {
        using type = void;
    };

    template <typename Dummy> struct aggregate_type_holder<true, Dummy> {
        using type = std::tuple_element_t<AggregateIndex, Filters>;
    };

    using Aggregate = typename aggregate_type_holder<HasAggregate>::type;

    static constexpr size_t ResultNumVars = [] {
        if constexpr (HasAggregate) {
            return static_cast<size_t>(aggregate_traits<Aggregate>::out_var_id +
                                       1);
        } else {
            return NumVars;
        }
    }();

    template <size_t H> auto *head_pred() const {
        return std::get<H>(heads).pred;
    }

    static_assert(
        [] {
            if constexpr (HasAggregate) {
                return all_aggregate_heads_covered<HeadTuple, Aggregate>(
                    std::make_index_sequence<NumHeads>{});
            } else {
                return all_heads_covered<HeadTuple, NumVars>(
                    std::make_index_sequence<NumHeads>{});
            }
        }(),
        "All aggregate head vars must be grouping vars "
        "or the output var");

    static_assert(
        [] {
            if constexpr (!HasAggregate) {
                return true;
            } else {
                constexpr int out_id = aggregate_traits<Aggregate>::out_var_id;
                constexpr int value_id =
                    aggregate_traits<Aggregate>::value_var_id;

                if (out_id != static_cast<int>(NumVars))
                    return false;
                if (value_id >= 0 && value_id >= static_cast<int>(NumVars))
                    return false;
                if (out_id == value_id)
                    return false;

                for (int group_id :
                     aggregate_traits<Aggregate>::group_var_ids) {
                    if (group_id < 0 || group_id >= static_cast<int>(NumVars))
                        return false;
                    if (group_id == out_id)
                        return false;
                }

                return aggregate_group_ids_unique<Aggregate>();
            }
        }(),
        "Invalid aggregate: input and group variables must use"
        " bound positive body atoms and group variables must be"
        " unique");

    static_assert(
        [] {
            if constexpr (has_duplicate_var_atom<Atoms>()) {
                return false;
            } else {
                return true;
            }
        }(),
        "Duplicated vars currently unsupported");

    // Execute a semi-naive delta step over the newly produced facts
    void operator()() const {
        if constexpr (!HasAggregate) {
            for_indices<NumAtoms>([&]<size_t S>() { do_source<S>(); });
        } else {
            bool recomputed = false;
            for_indices<NumAtoms>([&]<size_t S>() {
                if (!recomputed && !std::get<S>(atoms).pred->var.recent().empty()) {
                    recomputed = true;
                    do_agg_source_full<S>();
                }
            });
        }
    }

    // Execute a semi-naive step over all stable facts
    void eval_full() const {
        size_t best_source = NumAtoms;
        size_t best_size = std::numeric_limits<size_t>::max();

        for_indices<NumAtoms>([&]<size_t S>() {
            const auto *pred = std::get<S>(atoms).pred;
            const size_t size = pred->var.num_stable();
            if (size < best_size) {
                best_size = size;
                best_source = S;
            }
        });

        for_indices<NumAtoms>([&]<size_t S>() {
            if (best_source == S) {
                if constexpr (HasAggregate)
                    do_agg_source_full<S>();
                else
                    do_source_full<S>();
            }
        });
    }

    template <size_t S, size_t I>
    static constexpr std::array<int, atom_arities<Atoms>()[I]>
    atom_reindex_perm() {
        constexpr size_t a = atom_arities<Atoms>()[I];
        constexpr auto ids = atom_ids<Atoms>();
        constexpr auto vp = invert<NumVars>(make_order<Atoms>(S));

        std::array<int, a> perm{};
        std::iota(perm.begin(), perm.end(), 0);

        std::sort(perm.begin(), perm.end(),
                  [&](int i, int j) { return vp[ids[I][i]] < vp[ids[I][j]]; });

        return perm;
    }

    template <size_t S> static constexpr auto atom_var_table() {
        constexpr auto ids = atom_ids<Atoms>();
        constexpr auto arities = atom_arities<Atoms>();
        constexpr auto vp = invert<NumVars>(make_order<Atoms>(S));
        std::array<std::array<int, MAX_ARITY>, NumAtoms> table{};
        for (auto &row : table)
            row.fill(-1);
        for_indices<NumAtoms>([&]<size_t I>() {
            constexpr auto perm = atom_reindex_perm<S, I>();
            for (size_t col = 0; col < arities[I]; col++)
                table[I][col] = vp[ids[I][perm[col]]];
        });
        return table;
    }

    template <size_t S, size_t I>
    std::vector<std::span<const std::array<V, atom_arities<Atoms>()[I]>>>
    atom_spans(
        std::span<const std::array<V, atom_arities<Atoms>()[S]>> src) const {
        constexpr size_t a = atom_arities<Atoms>()[I];
        std::vector<std::span<const std::array<V, a>>> spans;
        if constexpr (I == S) {
            spans.emplace_back(src);
        } else {
            const void *idx = index_batches[S][I];
            if (idx != nullptr) {
                const auto *batches = static_cast<
                    const std::vector<std::vector<std::array<V, a>>> *>(idx);
                spans.reserve(batches->size());
                for (const auto &b : *batches)
                    spans.emplace_back(b);
            } else {
                auto *pred = std::get<I>(atoms).pred;
                spans.reserve(pred->var.stable.size());
                for (const auto &batch : pred->var.stable)
                    spans.emplace_back(batch.elements);
            }
        }
        return spans;
    }

    template <size_t S, size_t F> static constexpr size_t filter_bound() {
        using Filt = std::tuple_element_t<F, Filters>;
        if constexpr (is_aggregate_filter_v<Filt>) {
            return NumVars;
        } else {
            constexpr auto ids = filter_var_ids<Filt>::value;
            static_assert(
                [ids] {
                    for (int id : ids) {
                        if (id < 0 || id >= static_cast<int>(NumVars))
                            return false;
                    }
                    return true;
                }(),
                "filter var not bound by body atom");
            constexpr auto var_pos = invert<NumVars>(make_order<Atoms>(S));
            size_t bound = 0;
            for (int id : ids)
                bound = std::max(
                    bound, static_cast<size_t>(var_pos[id] + 1));
            return bound;
        }
    }

    template <size_t S>
    auto make_filter_test() const {
        return
            [this]<size_t BoundVars>(const std::array<V, NumVars> &row) {
                    return [&]<size_t... Fs>(std::index_sequence<Fs...>) {
                        return ([&] {
                            if constexpr (filter_bound<S, Fs>() == BoundVars) {
                                constexpr auto var_positions =
                                    invert<NumVars>(make_order<Atoms>(S));
                                return filter_check<Fs>(row, var_positions);
                            } else {
                                return true;
                            }
                    }() && ...);
                }(std::make_index_sequence<std::tuple_size_v<Filters>>{});
            };
    }

    template <size_t S, size_t I>
    static auto make_query_iter(
        const std::vector<
            std::span<const std::array<V, atom_arities<Atoms>()[I]>>> &spans) {
        if constexpr (I == S) {
            return lftj::TrieIterator<V, atom_arities<Atoms>()[I]>(spans.front());
        } else {
            return lftj::MergedTrieIterator<V, atom_arities<Atoms>()[I]>(spans);
        }
    }

    template <size_t S, typename Emit>
    void for_each_match(
        std::span<const std::array<V, atom_arities<Atoms>()[S]>> src,
        const Emit &emit) const {
        if (src.empty()) {
            return;
        }

        auto spans = [&]<size_t... Is>(std::index_sequence<Is...>) {
            return std::make_tuple(atom_spans<S, Is>(src)...);
        }(std::make_index_sequence<NumAtoms>{});
        auto iters = [&]<size_t... Is>(std::index_sequence<Is...>) {
            return std::make_tuple(
                make_query_iter<S, Is>(std::get<Is>(spans))...);
        }(std::make_index_sequence<NumAtoms>{});

        lftj::triejoin<V, NumVars, atom_var_table<S>()>(
            iters, emit, make_filter_test<S>());
    }

    // Depth-first LFTJ over all body atoms, reindexed to the source binding
    // order
    template <size_t S>
    void do_source_impl(
        std::span<const std::array<V, atom_arities<Atoms>()[S]>> src) const {
        if (src.empty()) {
            return;
        }

        constexpr size_t FLUSH_ROWS = size_t{1} << 16;
        auto batches = [&]<size_t... Hs>(std::index_sequence<Hs...>) {
            return std::make_tuple(
                std::vector<
                    std::array<V, atom_traits<HeadAt<Hs>>::arity>>{}...);
        }(std::make_index_sequence<NumHeads>{});
        for_indices<NumHeads>(
            [&]<size_t H>() { std::get<H>(batches).reserve(FLUSH_ROWS); });

        auto flush_head = [&]<size_t H>() {
            auto &b = std::get<H>(batches);
            if (b.empty())
                return;
            constexpr size_t head_arity = atom_traits<HeadAt<H>>::arity;
            constexpr auto var_plan = invert<NumVars>(make_order<Atoms>(S));
            constexpr auto head_positions =
                project<head_arity>(var_plan, atom_traits<HeadAt<H>>::var_ids);

            using ResultTuple = std::array<V, head_arity>;
            using ResultRelation = df::Relation<ResultTuple>;

            if constexpr (is_identity_prefix(head_positions)) {
                b.erase(std::unique(b.begin(), b.end()), b.end());

                head_pred<H>()->insert(
                    ResultRelation::from_sorted_unique_vec(std::move(b)));
            } else {
                head_pred<H>()->insert(ResultRelation::from_vec(std::move(b)));
            }

            b.clear();
            b.reserve(FLUSH_ROWS);
        };

        for_each_match<S>(src, [&](const std::array<V, NumVars> &row) {
            for_indices<NumHeads>([&]<size_t H>() {
                constexpr size_t ha = atom_traits<HeadAt<H>>::arity;
                constexpr auto vp = invert<NumVars>(make_order<Atoms>(S));
                constexpr auto hpos =
                    project<ha>(vp, atom_traits<HeadAt<H>>::var_ids);
                auto &b = std::get<H>(batches);
                b.push_back(project<ha>(row, hpos));
                if (b.size() >= FLUSH_ROWS)
                    flush_head.template operator()<H>();
            });
        });
        for_indices<NumHeads>(
            [&]<size_t H>() { flush_head.template operator()<H>(); });
    }

    template <size_t S> void do_source() const {
        auto *source_pred = std::get<S>(atoms).pred;
        do_source_impl<S>(source_pred->var.recent());
    }

    // When starting a new stratum we need to iterate over all
    // the committed facts recorded in var.stable
    template <size_t S> void do_source_full() const {
        constexpr size_t source_arity = atom_arities<Atoms>()[S];
        auto *source_pred = std::get<S>(atoms).pred;
        for (const auto &batch : source_pred->var.stable) {
            do_source_impl<S>(std::span<const std::array<V, source_arity>>(
                batch.elements));
        }
    }

    template <size_t F>
    bool filter_check(const std::array<V, NumVars> &tuple,
                      const std::array<int, NumVars> &var_positions) const {
        using Filt = std::tuple_element_t<F, Filters>;
        if constexpr (is_aggregate_filter_v<Filt>) {
            return true;
        } else if constexpr (is_expression_filter<Filt>::value) {
            constexpr auto ids = expression_filter_var_ids_impl<Filt>::value;
            static_assert(
                [ids] {
                    for (int id : ids) {
                        if (id < 0 || id >= static_cast<int>(NumVars))
                            return false;
                    }
                    return true;
                }(),
                "expression filter not bound by positive body atom");
            return [&]<size_t... Is>(std::index_sequence<Is...>) {
                return std::get<F>(filters).func(
                    tuple[var_positions[ids[Is]]]...);
            }(std::make_index_sequence<ids.size()>{});
        } else if constexpr (is_negated<Filt>::value) {
            constexpr auto ids = negated_var_ids<Filt>::value;
            constexpr size_t arity = negated_var_ids<Filt>::arity;
            static_assert(
                [ids] {
                    for (int id : ids) {
                        if (id < 0 || id >= static_cast<int>(NumVars))
                            return false;
                    }
                    return true;
                }(),
                "negated variable not bound by positive body atom");
            std::array<V, arity> key;
            for (size_t i = 0; i < arity; i++)
                key[i] = tuple[var_positions[ids[i]]];
            return !std::get<F>(filters).pred->stable_contains(key);
        } else {
            constexpr int var_id_a = filter_vars<Filt>::a_id;
            constexpr int var_id_b = filter_vars<Filt>::b_id;
            static_assert(var_id_a >= 0 && var_id_b >= 0 &&
                              var_id_a < static_cast<int>(NumVars) &&
                              var_id_b < static_cast<int>(NumVars),
                          "filter variable not bound by a positive body atom");
            const int pos_a = var_positions[var_id_a];
            const int pos_b = var_positions[var_id_b];
            constexpr Cmp op = filter_vars<Filt>::op;

            return cmp_apply<op>(tuple[pos_a], tuple[pos_b]);
        }
    }

    auto make_agg_groups() const {
        static_assert(HasAggregate);
        using AT = aggregate_traits<Aggregate>;
        constexpr size_t NumGroups = AT::group_var_ids.size();
        constexpr bool rows_reduction = AT::value_var_id < 0;
        using GroupKey = std::array<V, NumGroups>;
        using AggValue =
            std::conditional_t<rows_reduction, std::array<V, NumVars>, V>;

        return std::unordered_map<GroupKey, std::vector<AggValue>,
                                  AggregateArrayHash<V, NumGroups>>{};
    }

    template <size_t S, typename Groups>
    void collect_agg_row(const std::array<V, NumVars> &tuple,
                             Groups &groups) const {
        using AT = aggregate_traits<Aggregate>;
        constexpr size_t NumGroups = AT::group_var_ids.size();
        constexpr auto var_positions = invert<NumVars>(make_order<Atoms>(S));

        typename Groups::key_type key{};
        for (size_t i = 0; i < NumGroups; i++)
            key[i] = tuple[var_positions[AT::group_var_ids[i]]];

        auto [group, inserted] = groups.try_emplace(std::move(key));
        (void)inserted;
        if constexpr (AT::value_var_id < 0) {
            // Present rows in variable-id order so the reducer can
            // index independentl from the join binding order.
            std::array<V, NumVars> row{};
            for (size_t id = 0; id < NumVars; id++)
                row[id] = tuple[var_positions[id]];
            group->second.push_back(std::move(row));
        } else {
            group->second.push_back(tuple[var_positions[AT::value_var_id]]);
        }
    }

    template <typename Groups>
    auto apply_agg(const Groups &groups) const {
        static_assert(HasAggregate);
        using AT = aggregate_traits<Aggregate>;
        constexpr size_t NumGroups = AT::group_var_ids.size();
        using ResultRow = std::array<V, ResultNumVars>;
        using AggValue = typename Groups::mapped_type::value_type;

        std::vector<ResultRow> result;
        result.reserve(groups.size());

        const auto &aggregate = std::get<AggregateIndex>(filters);
        for (const auto &[key, values] : groups) {
            ResultRow row{};

            for (size_t i = 0; i < NumGroups; i++)
                row[AT::group_var_ids[i]] = key[i];

            auto value = aggregate.func(
                std::span<const AggValue>(values.data(), values.size()));

            static_assert(std::is_convertible_v<decltype(value), V>,
                          "Aggregate reducer result must be "
                          "convertible to the predicate value type");
            row[AT::out_var_id] = static_cast<V>(std::move(value));

            result.push_back(std::move(row));
        }

        return result;
    }

    template <typename Rows>
    void materialize_agg_heads(const Rows &rows) const {
        for_indices<NumHeads>([&]<size_t H>() {
            constexpr size_t HeadArity = atom_traits<HeadAt<H>>::arity;
            constexpr auto HeadIds = atom_traits<HeadAt<H>>::var_ids;

            std::vector<std::array<V, HeadArity>> output;
            output.reserve(rows.size());

            for (const auto &row : rows) {
                std::array<V, HeadArity> projected{};
                for (size_t col = 0; col < HeadArity; col++)
                    projected[col] = row[HeadIds[col]];

                output.push_back(std::move(projected));
            }

            head_pred<H>()->insert(
                df::Relation<std::array<V, HeadArity>>::from_vec(
                    std::move(output)));
        });
    }

    template <size_t S> void do_agg_source_full() const {
        static_assert(HasAggregate);
        constexpr size_t source_arity = atom_arities<Atoms>()[S];

        auto groups = make_agg_groups();
        auto *source_pred = std::get<S>(atoms).pred;
        for (const auto &batch : source_pred->var.stable) {
            for_each_match<S>(
                std::span<const std::array<V, source_arity>>(batch.elements),
                [&](const std::array<V, NumVars> &row) {
                    collect_agg_row<S>(row, groups);
                });
        }

        auto aggregated = apply_agg(groups);
        materialize_agg_heads(aggregated);
    }
};

} // namespace df::datalog
