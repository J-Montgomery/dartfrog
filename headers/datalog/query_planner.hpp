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

static constexpr size_t MaxArity = 8;

template <typename T> struct atom_traits;
template <typename P, int... Ids> struct atom_traits<Term<P, Var<Ids>...>> {
    using pred_t = P;
    static constexpr size_t Arity = sizeof...(Ids);
    static constexpr std::array<int, sizeof...(Ids)> VarIdsValue = {Ids...};
};

template <typename Atoms> constexpr auto atom_ids() {
    constexpr size_t numAtoms = std::tuple_size_v<Atoms>;
    std::array<std::array<int, MaxArity>, numAtoms> result{};
    for (auto &row : result)
        row.fill(-1);
    for_indices<numAtoms>([&]<size_t I>() {
        using AT = atom_traits<std::tuple_element_t<I, Atoms>>;
        for (size_t j = 0; j < AT::Arity; j++)
            result[I][j] = AT::VarIdsValue[j];
    });
    return result;
}

template <typename Atoms> constexpr auto atom_arities() {
    constexpr size_t numAtoms = std::tuple_size_v<Atoms>;
    std::array<size_t, numAtoms> result{};
    for_indices<numAtoms>([&]<size_t I>() {
        result[I] = atom_traits<std::tuple_element_t<I, Atoms>>::Arity;
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
    constexpr size_t numVars = num_vars<Atoms>();
    constexpr size_t numAtoms = std::tuple_size_v<Atoms>;
    constexpr auto ids = atom_ids<Atoms>();
    constexpr auto arities = atom_arities<Atoms>();

    std::array<int, numVars> order{};
    std::array<bool, numVars> bound{};
    std::array<int, numVars> score{};

    bound.fill(false);
    score.fill(0);

    auto bind = [&](int var_id) {
        bound[var_id] = true;
        for (size_t atom = 0; atom < numAtoms; atom++) {
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
    while (num_bound < numVars) {
        int best_var = -1, best_score = -1;
        for (int var_id = 0; var_id < (int)numVars; var_id++)
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
template <typename T> struct IsNegated : std::false_type {};
template <typename P, typename... Vars>
struct IsNegated<NegatedTerm<P, Vars...>> : std::true_type {};

/* Expression filters */

template <typename T> struct IsExpressionFilter : std::false_type {};
template <typename Func, int... VarIds>
struct IsExpressionFilter<ExpressionFilter<Func, VarIds...>>
    : std::true_type {};

/* Aggregates */

template <typename T> struct IsAggregateFilter : std::false_type {};

template <typename Func, int OutVarId, int ValueVarId, int... GroupVarIds>
struct IsAggregateFilter<
    AggregateFilter<Func, OutVarId, ValueVarId, GroupVarIds...>>
    : std::true_type {};

template <typename Func, int OutVarId, int... GroupVarIds>
struct IsAggregateFilter<AggregateRowsFilter<Func, OutVarId, GroupVarIds...>>
    : std::true_type {};

template <typename T>
inline constexpr bool IsAggregateFilterV = IsAggregateFilter<T>::value;

template <typename Filters> constexpr size_t aggregate_count() {
    return []<typename... Ts>(std::type_identity<std::tuple<Ts...>>) {
        return (0 + ... + (IsAggregateFilterV<Ts> ? 1 : 0));
    }(std::type_identity<Filters>{});
}

template <typename Filters> constexpr size_t aggregate_index() {
    size_t result = std::tuple_size_v<Filters>;

    for_indices<std::tuple_size_v<Filters>>([&]<size_t I>() {
        using Filt = std::tuple_element_t<I, Filters>;
        if constexpr (IsAggregateFilterV<Filt>)
            result = I;
    });

    return result;
}

template <typename Agg> constexpr bool aggregate_group_ids_unique() {
    constexpr auto ids = Agg::GroupVarIds;

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

template <typename T> struct filter_traits;
template <typename Func, int... VarIdsT>
struct filter_traits<ExpressionFilter<Func, VarIdsT...>> {
    static constexpr std::array<int, sizeof...(VarIdsT)> VarIds{VarIdsT...};
};
template <typename P, int... IdsT>
struct filter_traits<NegatedTerm<P, Var<IdsT>...>> {
    static constexpr std::array<int, sizeof...(IdsT)> VarIds{IdsT...};
};
template <Cmp Op, int A, int B>
struct filter_traits<Compare<Op, Var<A>, Var<B>>> {
    static constexpr std::array<int, 2> VarIds = {A, B};
    static constexpr Cmp Operation = Op;
};

template <size_t NumVars, size_t N>
constexpr bool vars_bound(const std::array<int, N> &ids) {
    for (int id : ids)
        if (id < 0 || id >= static_cast<int>(NumVars))
            return false;
    return true;
}

template <typename Agg, size_t NumVars>
constexpr bool aggregate_inputs_valid() {
    return Agg::OutVarId == static_cast<int>(NumVars) &&
           (Agg::ValueVarId < 0 ||
            Agg::ValueVarId < static_cast<int>(NumVars)) &&
           vars_bound<NumVars>(Agg::GroupVarIds) &&
           aggregate_group_ids_unique<Agg>();
}

template <typename Filt, size_t NumVars>
constexpr bool validate_filter_vars() {
    if constexpr (!IsAggregateFilterV<Filt>) {
        constexpr bool bound = vars_bound<NumVars>(filter_traits<Filt>::VarIds);
        if constexpr (IsExpressionFilter<Filt>::value)
            static_assert(bound,
                          "expression filter not bound by body atom");
        else if constexpr (IsNegated<Filt>::value)
            static_assert(bound,
                          "negated variable not bound by body atom");
        else
            static_assert(bound,
                          "filter variable not bound by a body atom");
        return bound;
    }
    return true;
}


template <Cmp Op, typename T> constexpr bool cmp_apply(const T &x, const T &y) {
    if constexpr (Op == Cmp::Lt)
        return x < y;
    else if constexpr (Op == Cmp::Le)
        return x <= y;
    else if constexpr (Op == Cmp::Gt)
        return x > y;
    else if constexpr (Op == Cmp::Ge)
        return x >= y;
    else if constexpr (Op == Cmp::Ne)
        return x != y;
    else
        return x == y;
}

template <typename Atoms> constexpr bool has_duplicate_var_atom() {
    constexpr size_t numAtoms = std::tuple_size_v<Atoms>;
    constexpr auto ids = atom_ids<Atoms>();
    constexpr auto arities = atom_arities<Atoms>();
    for (size_t atom = 0; atom < numAtoms; atom++)
        for (size_t i = 0; i < arities[atom]; i++)
            for (size_t j = i + 1; j < arities[atom]; j++)
                if (ids[atom][i] == ids[atom][j])
                    return true;
    return false;
}

// True iff Head references only variable ids in [0, NumVars).
template <typename Head, size_t NumVars> constexpr bool one_head_covered() {
    return vars_bound<NumVars>(atom_traits<Head>::VarIdsValue);
}

// True iff every head in HeadTup references only variable ids in [0, NumVars).
template <typename HeadTup, size_t NumVars, size_t... Hs>
constexpr bool all_heads_covered(std::index_sequence<Hs...>) {
    return (one_head_covered<std::tuple_element_t<Hs, HeadTup>, NumVars>() &&
            ...);
}

template <typename Head, typename Agg>
constexpr bool one_aggregate_head_covered() {
    for (int id : atom_traits<Head>::VarIdsValue) {
        if (id < 0)
            return false;

        if (id == Agg::OutVarId)
            continue;

        bool is_group = false;
        for (int group_id : Agg::GroupVarIds) {
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
    struct AggregateTypeHolder {
        using type = void;
    };

    template <typename Dummy> struct AggregateTypeHolder<true, Dummy> {
        using type = std::tuple_element_t<AggregateIndex, Filters>;
    };

    using Aggregate = typename AggregateTypeHolder<HasAggregate>::type;

    static constexpr size_t ResultNumVars = [] {
        if constexpr (HasAggregate) {
            return static_cast<size_t>(Aggregate::OutVarId + 1);
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
            if constexpr (HasAggregate)
                return aggregate_inputs_valid<Aggregate, NumVars>();
            return true;
        }(),
        "Invalid aggregate: input and group variables must use"
        " bound positive body atoms and group variables must be"
        " unique");

    static_assert(!has_duplicate_var_atom<Atoms>(),
                  "Duplicated vars currently unsupported");

    static_assert([]<size_t... Fs>(std::index_sequence<Fs...>) {
        return (validate_filter_vars<std::tuple_element_t<Fs, Filters>,
                                     NumVars>() && ...);
    }(std::make_index_sequence<std::tuple_size_v<Filters>>{}));

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
        std::array<std::array<int, MaxArity>, NumAtoms> table{};
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
        if constexpr (IsAggregateFilterV<Filt>) {
            return NumVars;
        } else {
            constexpr auto ids = filter_traits<Filt>::VarIds;
            constexpr auto varPos = invert<NumVars>(make_order<Atoms>(S));
            size_t bound = 0;
            for (int id : ids)
                bound = std::max(
                    bound, static_cast<size_t>(varPos[id] + 1));
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
                                constexpr auto varPositions =
                                    invert<NumVars>(make_order<Atoms>(S));
                                return filter_check<Fs>(row, varPositions);
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

        constexpr size_t flushRows = size_t{1} << 16;
        auto batches = [&]<size_t... Hs>(std::index_sequence<Hs...>) {
            return std::make_tuple(
                std::vector<
                    std::array<V, atom_traits<HeadAt<Hs>>::Arity>>{}...);
        }(std::make_index_sequence<NumHeads>{});
        for_indices<NumHeads>(
            [&]<size_t H>() { std::get<H>(batches).reserve(flushRows); });

        auto flush_head = [&]<size_t H>() {
            auto &b = std::get<H>(batches);
            if (b.empty())
                return;
            constexpr size_t headArity = atom_traits<HeadAt<H>>::Arity;
            constexpr auto varPlan = invert<NumVars>(make_order<Atoms>(S));
            constexpr auto headPositions =
                project<headArity>(varPlan, atom_traits<HeadAt<H>>::VarIdsValue);

            using ResultTuple = std::array<V, headArity>;
            using ResultRelation = df::Relation<ResultTuple>;

            if constexpr (is_identity_prefix(headPositions)) {
                b.erase(std::unique(b.begin(), b.end()), b.end());

                head_pred<H>()->insert(
                    ResultRelation::from_sorted_unique_vec(std::move(b)));
            } else {
                head_pred<H>()->insert(ResultRelation::from_vec(std::move(b)));
            }

            b.clear();
            b.reserve(flushRows);
        };

        for_each_match<S>(src, [&](const std::array<V, NumVars> &row) {
            for_indices<NumHeads>([&]<size_t H>() {
                constexpr size_t ha = atom_traits<HeadAt<H>>::Arity;
                constexpr auto vp = invert<NumVars>(make_order<Atoms>(S));
                constexpr auto hpos =
                    project<ha>(vp, atom_traits<HeadAt<H>>::VarIdsValue);
                auto &b = std::get<H>(batches);
                b.push_back(project<ha>(row, hpos));
                if (b.size() >= flushRows)
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
        constexpr size_t sourceArity = atom_arities<Atoms>()[S];
        auto *source_pred = std::get<S>(atoms).pred;
        for (const auto &batch : source_pred->var.stable) {
            do_source_impl<S>(std::span<const std::array<V, sourceArity>>(
                batch.elements));
        }
    }

    template <size_t F>
    bool filter_check(const std::array<V, NumVars> &tuple,
                      const std::array<int, NumVars> &var_positions) const {
        using Filt = std::tuple_element_t<F, Filters>;
        if constexpr (IsAggregateFilterV<Filt>) {
            return true;
        } else if constexpr (IsExpressionFilter<Filt>::value) {
            constexpr auto ids = filter_traits<Filt>::VarIds;
            return [&]<size_t... Is>(std::index_sequence<Is...>) {
                return std::get<F>(filters).func(
                    tuple[var_positions[ids[Is]]]...);
            }(std::make_index_sequence<ids.size()>{});
        } else if constexpr (IsNegated<Filt>::value) {
            constexpr auto ids = filter_traits<Filt>::VarIds;
            std::array<V, ids.size()> key;
            for (size_t i = 0; i < ids.size(); i++)
                key[i] = tuple[var_positions[ids[i]]];
            return !std::get<F>(filters).pred->stable_contains(key);
        } else {
            constexpr auto ids = filter_traits<Filt>::VarIds;
            return cmp_apply<filter_traits<Filt>::Operation>(
                tuple[var_positions[ids[0]]], tuple[var_positions[ids[1]]]);
        }
    }

    auto make_agg_groups() const {
        static_assert(HasAggregate);
        constexpr size_t numGroups = Aggregate::GroupVarIds.size();
        constexpr bool rowsReduction = Aggregate::ValueVarId < 0;
        using GroupKey = std::array<V, numGroups>;
        using AggValue =
            std::conditional_t<rowsReduction, std::array<V, NumVars>, V>;

        return std::unordered_map<GroupKey, std::vector<AggValue>,
                                  AggregateArrayHash<V, numGroups>>{};
    }

    template <size_t S, typename Groups>
    void collect_agg_row(const std::array<V, NumVars> &tuple,
                             Groups &groups) const {
        constexpr size_t numGroups = Aggregate::GroupVarIds.size();
        constexpr auto varPositions = invert<NumVars>(make_order<Atoms>(S));

        typename Groups::key_type key{};
        for (size_t i = 0; i < numGroups; i++)
            key[i] = tuple[varPositions[Aggregate::GroupVarIds[i]]];

        auto [group, inserted] = groups.try_emplace(std::move(key));
        (void)inserted;
        if constexpr (Aggregate::ValueVarId < 0) {
            // Present rows in variable-id order so the reducer can
            // index independentl from the join binding order.
            std::array<V, NumVars> row{};
            for (size_t id = 0; id < NumVars; id++)
                row[id] = tuple[varPositions[id]];
            group->second.push_back(std::move(row));
        } else {
            group->second.push_back(tuple[varPositions[Aggregate::ValueVarId]]);
        }
    }

    template <typename Groups>
    auto apply_agg(const Groups &groups) const {
        static_assert(HasAggregate);
        constexpr size_t numGroups = Aggregate::GroupVarIds.size();
        using ResultRow = std::array<V, ResultNumVars>;
        using AggValue = typename Groups::mapped_type::value_type;

        std::vector<ResultRow> result;
        result.reserve(groups.size());

        const auto &aggregate = std::get<AggregateIndex>(filters);
        for (const auto &[key, values] : groups) {
            ResultRow row{};

            for (size_t i = 0; i < numGroups; i++)
                row[Aggregate::GroupVarIds[i]] = key[i];

            auto value = aggregate.func(
                std::span<const AggValue>(values.data(), values.size()));

            static_assert(std::is_convertible_v<decltype(value), V>,
                          "Aggregate reducer result must be "
                          "convertible to the predicate value type");
            row[Aggregate::OutVarId] = static_cast<V>(std::move(value));

            result.push_back(std::move(row));
        }

        return result;
    }

    template <typename Rows>
    void materialize_agg_heads(const Rows &rows) const {
        for_indices<NumHeads>([&]<size_t H>() {
            constexpr size_t headArity = atom_traits<HeadAt<H>>::Arity;
            constexpr auto headIds = atom_traits<HeadAt<H>>::VarIdsValue;

            std::vector<std::array<V, headArity>> output;
            output.reserve(rows.size());

            for (const auto &row : rows) {
                std::array<V, headArity> projected{};
                for (size_t col = 0; col < headArity; col++)
                    projected[col] = row[headIds[col]];

                output.push_back(std::move(projected));
            }

            head_pred<H>()->insert(
                df::Relation<std::array<V, headArity>>::from_vec(
                    std::move(output)));
        });
    }

    template <size_t S> void do_agg_source_full() const {
        static_assert(HasAggregate);
        constexpr size_t sourceArity = atom_arities<Atoms>()[S];

        auto groups = make_agg_groups();
        auto *source_pred = std::get<S>(atoms).pred;
        for (const auto &batch : source_pred->var.stable) {
            for_each_match<S>(
                std::span<const std::array<V, sourceArity>>(batch.elements),
                [&](const std::array<V, NumVars> &row) {
                    collect_agg_row<S>(row, groups);
                });
        }

        auto aggregated = apply_agg(groups);
        materialize_agg_heads(aggregated);
    }
};

} // namespace df::datalog
