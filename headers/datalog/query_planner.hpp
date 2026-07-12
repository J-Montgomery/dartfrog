#pragma once

#include <algorithm>
#include <array>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "dartfrog/join.hpp"
#include "dartfrog/leapers.hpp"
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
    for (auto &flag : bound)
        flag = false;
    for (auto &sc : score)
        sc = 0;

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

struct ExtSpec {
    size_t atom;
    size_t propose_col;
};

template <size_t NumAtoms> struct LevelPlan {
    std::array<ExtSpec, NumAtoms> entries{};
    size_t count = 0;
};

// Generate an execution plan for a single stratum
// Variables are ordered and greedily bound to determine which
// atom relations that will be proposed by the leaper when we solve()
template <typename Atoms>
constexpr auto level_plan(size_t src_idx, size_t bound_vars) {
    constexpr size_t NumAtoms = std::tuple_size_v<Atoms>;
    constexpr size_t NumVars = num_vars<Atoms>();
    constexpr auto ids = atom_ids<Atoms>();
    constexpr auto arities = atom_arities<Atoms>();
    auto var_positions = invert<NumVars>(make_order<Atoms>(src_idx));
    LevelPlan<NumAtoms> plan{};
    for (size_t atom = 0; atom < NumAtoms; atom++) {
        if (atom == src_idx)
            continue;
        size_t arity = arities[atom];
        int propose_col = -1;
        bool prefix_ok = true;
        for (size_t col = 0; col < arity; col++) {
            if (ids[atom][col] < 0) {
                continue;
            }

            int pos = var_positions[ids[atom][col]];
            if (pos == static_cast<int>(bound_vars)) {
                propose_col = static_cast<int>(col);
                break;
            } else if (pos > static_cast<int>(bound_vars)) {
                prefix_ok = false;
                break;
            }
        }
        if (!prefix_ok || propose_col < 0) {
            continue;
        }
        plan.entries[plan.count++] = {atom, static_cast<size_t>(propose_col)};
    }
    return plan;
}

// When a bound variable (var_positions[id] < bound_vars)
// sits at a column *after* propose_col, the leaper cannot form a prefix key
// and enumerates the entire relation.
template <typename Atoms>
constexpr bool plan_has_scan_trap(size_t src_idx, size_t bound_vars) {
    constexpr size_t NumAtoms = std::tuple_size_v<Atoms>;
    constexpr size_t NumVars = num_vars<Atoms>();
    constexpr auto ids = atom_ids<Atoms>();
    constexpr auto arities = atom_arities<Atoms>();
    auto var_positions = invert<NumVars>(make_order<Atoms>(src_idx));
    for (size_t atom = 0; atom < NumAtoms; atom++) {
        if (atom == src_idx)
            continue;
        size_t arity = arities[atom];
        int propose_col = -1;
        bool prefix_ok = true;
        for (size_t col = 0; col < arity; col++) {
            if (ids[atom][col] < 0)
                continue;
            int pos = var_positions[ids[atom][col]];
            if (pos == static_cast<int>(bound_vars)) {
                propose_col = static_cast<int>(col);
                break;
            } else if (pos > static_cast<int>(bound_vars)) {
                prefix_ok = false;
                break;
            }
        }
        if (!prefix_ok || propose_col < 0)
            continue;
        for (size_t col = static_cast<size_t>(propose_col) + 1; col < arity;
             col++)
            if (ids[atom][col] >= 0 &&
                var_positions[ids[atom][col]] < static_cast<int>(bound_vars))
                return true;
    }
    return false;
}

template <bool Trap> struct ScanTrapWarning {};
template <> struct ScanTrapWarning<true> {
    [[deprecated(
        "query plan requires a full scan because a bound variable "
        "appears after the declared column. This probably isn't what you want. "
        "Reindex the relation with make_reindexed so the "
        "join key is in a leading column.")]] ScanTrapWarning() = default;
};

template <typename T> struct is_negated : std::false_type {};
template <typename P, typename... Vars>
struct is_negated<NegatedTerm<P, Vars...>> : std::true_type {};

template <typename T> struct negated_var_ids;
template <typename P, int... Ids>
struct negated_var_ids<NegatedTerm<P, Var<Ids>...>> {
    static constexpr size_t arity = sizeof...(Ids);
    static constexpr std::array<int, sizeof...(Ids)> value = {Ids...};
};

template <typename T> struct is_expression_filter : std::false_type {};
template <typename Func, int... VarIds>
struct is_expression_filter<ExpressionFilter<Func, VarIds...>>
    : std::true_type {};

template <typename T> struct expression_filter_var_ids_impl;
template <typename Func, int... VarIds>
struct expression_filter_var_ids_impl<ExpressionFilter<Func, VarIds...>> {
    static constexpr std::array<int, sizeof...(VarIds)> value = {VarIds...};
};

template <typename T> struct filter_vars;
template <Cmp Op, int A, int B>
struct filter_vars<Compare<Op, Var<A>, Var<B>>> {
    static constexpr int a_id = A;
    static constexpr int b_id = B;
    static constexpr Cmp op = Op;
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

template <size_t S, typename Atoms, typename Filters>
constexpr bool has_residual_filters() {
    if constexpr (std::tuple_size_v<Filters> > 0) {
        return true;
    } else {
        constexpr size_t NumAtoms = std::tuple_size_v<Atoms>;
        constexpr size_t NumVars = num_vars<Atoms>();
        constexpr auto ids = atom_ids<Atoms>();
        constexpr auto arities = atom_arities<Atoms>();
        constexpr auto var_positions = invert<NumVars>(make_order<Atoms>(S));
        for (size_t atom = 0; atom < NumAtoms; atom++) {
            if (atom == S)
                continue;
            size_t arity = arities[atom];

            for (size_t col = 0; col < arity; col++)
                for (size_t d = col + 1; d < arity; d++)
                    if (ids[atom][col] == ids[atom][d])
                        return true;

            bool forward = true;
            for (size_t col = 1; col < arity; col++)
                if (ids[atom][col] < 0 ||
                    var_positions[ids[atom][col]] <=
                        var_positions[ids[atom][col - 1]]) {
                    forward = false;
                    break;
                }
            if (!forward)
                return true;

            // All vars are bound in the source prefix
            constexpr size_t src_arity = atom_arities<Atoms>()[S];
            int max_pos = -1;
            for (size_t col = 0; col < arity; col++)
                if (ids[atom][col] >= 0 &&
                    var_positions[ids[atom][col]] > max_pos)
                    max_pos = var_positions[ids[atom][col]];
            if (max_pos < (int)src_arity)
                return true;
        }
        return false;
    }
}

template <typename V, size_t K> struct ArrayAppender {
    constexpr std::array<V, K + 1> operator()(const std::array<V, K> &prefix,
                                              const V &new_val) const {
        std::array<V, K + 1> result;
        for (size_t i = 0; i < K; i++)
            result[i] = prefix[i];
        result[K] = new_val;
        return result;
    }
};

template <size_t Atom, size_t ProposeCol, typename V, size_t K, typename Atoms,
          size_t NumVars>
auto make_ext(const Atoms &atoms,
              const std::array<int, NumVars> &var_positions) {
    constexpr auto ids = atom_ids<Atoms>();
    constexpr auto arities = atom_arities<Atoms>();
    constexpr size_t N = arities[Atom];

    std::array<int, ProposeCol> key_positions{};
    for (size_t col = 0; col < ProposeCol; col++)
        key_positions[col] = var_positions[ids[Atom][col]];

    auto *pred = std::get<Atom>(atoms).pred;
    df::PrefixExtractor<V, K, ProposeCol> extractor{key_positions};
    return df::TupleLeaper<V, N, ProposeCol,
                           df::PrefixExtractor<V, K, ProposeCol>>{
        &pred->var.stable, std::move(extractor)};
}

template <typename V, size_t K, typename... Exts>
auto to_coll(std::tuple<Exts...> &&t) {
    return df::LeaperCollection<std::array<V, K>, V, Exts...>{std::move(t)};
}

template <typename V, size_t K, size_t S, size_t Klvl, typename Atoms,
          size_t... Js>
auto build_exts(const Atoms &atoms, std::index_sequence<Js...>) {
    constexpr auto plan = level_plan<Atoms>(S, Klvl);
    [[maybe_unused]] ScanTrapWarning<plan_has_scan_trap<Atoms>(S, Klvl)>
        scan_trap_check{};
    constexpr size_t NumVars = num_vars<Atoms>();
    constexpr auto var_positions = invert<NumVars>(make_order<Atoms>(S));
    return std::make_tuple(
        make_ext<plan.entries[Js].atom, plan.entries[Js].propose_col, V, K>(
            atoms, var_positions)...);
}

template <typename V, size_t NumVars, size_t S, size_t K, typename Atoms>
df::Relation<std::array<V, NumVars>>
extend(df::Relation<std::array<V, K>> prefix, const Atoms &atoms) {
    if constexpr (K == NumVars) {
        return std::move(prefix);
    } else {
        constexpr auto plan = level_plan<Atoms>(S, K);
        auto extractors = build_exts<V, K, S, K, Atoms>(
            atoms, std::make_index_sequence<plan.count>{});
        auto leapers = to_coll<V, K>(std::move(extractors));
        auto next =
            df::leapjoin(std::span<const std::array<V, K>>(prefix.elements),
                         leapers, ArrayAppender<V, K>{});
        return extend<V, NumVars, S, K + 1>(std::move(next), atoms);
    }
}

template <typename V, size_t NumVars, size_t S, size_t K, typename Atoms>
df::Relation<std::array<V, NumVars>>
extend(std::span<const std::array<V, K>> src, const Atoms &atoms) {
    if constexpr (K == NumVars) {

        return df::Relation<std::array<V, NumVars>>{
            std::vector<std::array<V, NumVars>>(src.begin(), src.end())};
    } else {
        constexpr auto plan = level_plan<Atoms>(S, K);
        auto extractors = build_exts<V, K, S, K, Atoms>(
            atoms, std::make_index_sequence<plan.count>{});
        auto leapers = to_coll<V, K>(std::move(extractors));
        auto next = df::leapjoin(src, leapers, ArrayAppender<V, K>{});
        return extend<V, NumVars, S, K + 1>(std::move(next), atoms);
    }
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

// Normalize a rule's head spec to a tuple of head Terms, so
// a single Term<...> becomes a one-element tuple and
// a MultiHead<Hs...> unwraps to its heads.
template <typename H> struct head_tuple_of {
    using type = std::tuple<H>;
};
template <typename... Hs> struct head_tuple_of<MultiHead<Hs...>> {
    using type = std::tuple<Hs...>;
};

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

template <typename HeadTerm, typename Atoms, typename Filters>
struct QueryPlanner {
    HeadTerm head;
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

    using HeadTuple = typename head_tuple_of<HeadTerm>::type;
    static constexpr size_t NumHeads = std::tuple_size_v<HeadTuple>;

    template <size_t H> using HeadAt = std::tuple_element_t<H, HeadTuple>;

    template <size_t H> auto *head_pred() const {
        if constexpr (is_multihead<HeadTerm>::value)
            return std::get<H>(head.heads).pred;
        else
            return head.pred;
    }

    static_assert(all_heads_covered<HeadTuple, NumVars>(
                      std::make_index_sequence<NumHeads>{}),
                  "All head variables must appear in the body");

    template <size_t S> static constexpr bool source_is_viable() {
        constexpr size_t source_arity = atom_arities<Atoms>()[S];
        for (size_t K = source_arity; K < NumVars; K++)
            if (level_plan<Atoms>(S, K).count == 0)
                return false;
        return true;
    }

    // Execute a semi-naive delta step over the newly produced facts
    void operator()() const {
        for_indices<NumAtoms>([&]<size_t S>() { do_source<S>(); });
    }

    // Execute a semi-naive step over all stable facts
    void eval_full() const {
        for_indices<NumAtoms>([&]<size_t S>() { do_source_full<S>(); });
    }

    template <size_t S, size_t I>
    static constexpr std::array<int, atom_arities<Atoms>()[I]>
    atom_reindex_perm() {
        constexpr size_t a = atom_arities<Atoms>()[I];
        constexpr auto ids = atom_ids<Atoms>();
        constexpr auto vp = invert<NumVars>(make_order<Atoms>(S));
        std::array<int, a> perm{};
        for (size_t i = 0; i < a; i++)
            perm[i] = static_cast<int>(i);
        for (size_t i = 0; i < a; i++)
            for (size_t j = i + 1; j < a; j++)
                if (vp[ids[I][perm[j]]] < vp[ids[I][perm[i]]])
                    std::swap(perm[i], perm[j]);
        return perm;
    }

    template <size_t S, size_t I> static std::vector<int> atom_reindex_vars() {
        constexpr size_t a = atom_arities<Atoms>()[I];
        constexpr auto ids = atom_ids<Atoms>();
        constexpr auto vp = invert<NumVars>(make_order<Atoms>(S));
        constexpr auto perm = atom_reindex_perm<S, I>();
        std::vector<int> vars(a);
        for (size_t k = 0; k < a; k++)
            vars[k] = vp[ids[I][perm[k]]];
        return vars;
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

    template <size_t S>
    auto make_filter_test(const std::array<int, NumVars> &var_positions) const {
        return
            [this, var_positions](const std::array<V, NumVars> &row) -> bool {
                return [&]<size_t... Fs>(std::index_sequence<Fs...>) {
                    return (filter_check<Fs>(row, var_positions) && ...);
                }(std::make_index_sequence<std::tuple_size_v<Filters>>{});
            };
    }

    template <size_t I>
    static lftj::MergedTrieIterator<V, atom_arities<Atoms>()[I]>
    make_merged_iter(
        const std::vector<
            std::span<const std::array<V, atom_arities<Atoms>()[I]>>> &spans) {
        return lftj::MergedTrieIterator<V, atom_arities<Atoms>()[I]>(spans);
    }

    // Depth-first LFTJ over all body atoms, reindexed to the source binding
    // order
    template <size_t S>
    void do_source_impl_lftj(
        std::span<const std::array<V, atom_arities<Atoms>()[S]>> src) const {
        constexpr auto var_positions = invert<NumVars>(make_order<Atoms>(S));

        auto spans = [&]<size_t... Is>(std::index_sequence<Is...>) {
            return std::make_tuple(atom_spans<S, Is>(src)...);
        }(std::make_index_sequence<NumAtoms>{});
        auto iters = [&]<size_t... Is>(std::index_sequence<Is...>) {
            return std::make_tuple(
                make_merged_iter<Is>(std::get<Is>(spans))...);
        }(std::make_index_sequence<NumAtoms>{});

        std::vector<lftj::AnyTrie<V>> erased;
        erased.reserve(NumAtoms);
        [&]<size_t... Is>(std::index_sequence<Is...>) {
            (erased.push_back(lftj::erase_trie(std::get<Is>(iters))), ...);
        }(std::make_index_sequence<NumAtoms>{});
        std::vector<lftj::AtomPlan<V>> plans;
        plans.reserve(NumAtoms);
        [&]<size_t... Is>(std::index_sequence<Is...>) {
            (plans.push_back(
                 lftj::AtomPlan<V>{&erased[Is], atom_reindex_vars<S, Is>()}),
             ...);
        }(std::make_index_sequence<NumAtoms>{});

        [[maybe_unused]] auto keep = make_filter_test<S>(var_positions);
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
            constexpr size_t head_atom = atom_traits<HeadAt<H>>::arity;
            head_pred<H>()->insert(
                df::Relation<std::array<V, head_atom>>::from_vec(std::move(b)));
            b.clear();
            b.reserve(FLUSH_ROWS);
        };

        std::array<V, NumVars> row{};
        lftj::triejoin<V>(
            static_cast<int>(NumVars), plans, [&](const std::vector<V> &asg) {
                for (size_t i = 0; i < NumVars; i++)
                    row[i] = asg[i];
                // Check if the rule has any remaining filters to check
                if constexpr (std::tuple_size_v<Filters> > 0)
                    if (!keep(row))
                        return;
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

    // Breadth-first fallback (materializes each level) for rules with repeated
    // variables
    template <size_t S>
    void do_source_impl_extend(
        std::span<const std::array<V, atom_arities<Atoms>()[S]>> src) const {
        constexpr size_t source_arity = atom_arities<Atoms>()[S];
        auto joined_tuples = extend<V, NumVars, S, source_arity>(src, atoms);
        constexpr auto var_positions = invert<NumVars>(make_order<Atoms>(S));

        if constexpr (!has_residual_filters<S, Atoms, Filters>()) {
            for_indices<NumHeads>([&]<size_t H>() {
                constexpr size_t ha = atom_traits<HeadAt<H>>::arity;
                constexpr auto vp = invert<NumVars>(make_order<Atoms>(S));
                constexpr auto hpos =
                    project<ha>(vp, atom_traits<HeadAt<H>>::var_ids);
                head_pred<H>()->insert(
                    df::Relation<std::array<V, ha>>::from_map(
                        joined_tuples, [&](const std::array<V, NumVars> &row) {
                            return project<ha>(row, hpos);
                        }));
            });
        } else {
            auto keep = make_residual_test<S>(var_positions);
            std::vector<std::array<V, NumVars>> kept;
            kept.reserve(joined_tuples.elements.size());
            for (const auto &row : joined_tuples.elements)
                if (keep(row))
                    kept.push_back(row);
            for_indices<NumHeads>([&]<size_t H>() {
                constexpr size_t ha = atom_traits<HeadAt<H>>::arity;
                constexpr auto vp = invert<NumVars>(make_order<Atoms>(S));
                constexpr auto hpos =
                    project<ha>(vp, atom_traits<HeadAt<H>>::var_ids);
                std::vector<std::array<V, ha>> result;
                result.reserve(kept.size());
                for (const auto &row : kept)
                    result.push_back(project<ha>(row, hpos));
                head_pred<H>()->insert(
                    df::Relation<std::array<V, ha>>::from_vec(
                        std::move(result)));
            });
        }
    }

    template <size_t S>
    void do_source_impl(
        std::span<const std::array<V, atom_arities<Atoms>()[S]>> src) const {
        if (src.empty())
            return;
        if constexpr (has_duplicate_var_atom<Atoms>())
            do_source_impl_extend<S>(src);
        else
            do_source_impl_lftj<S>(src);
    }

    template <size_t S> static constexpr bool source_enabled() {
        return !has_duplicate_var_atom<Atoms>() || source_is_viable<S>();
    }

    template <size_t S> void do_source() const {
        if constexpr (source_enabled<S>()) {
            auto *source_pred = std::get<S>(atoms).pred;
            do_source_impl<S>(source_pred->var.recent());
        }
    }

    // When starting a new stratum we need to iterate over all
    // the committed facts recorded in var.stable
    template <size_t S> void do_source_full() const {
        constexpr size_t source_arity = atom_arities<Atoms>()[S];
        if constexpr (source_enabled<S>()) {
            auto *source_pred = std::get<S>(atoms).pred;
            for (const auto &batch : source_pred->var.stable)
                do_source_impl<S>(std::span<const std::array<V, source_arity>>(
                    batch.elements));
        }
    }

    template <size_t S, size_t I>
    static constexpr bool atom_needs_semijoin_check() {
        if constexpr (I == S)
            return false;
        constexpr auto ids = atom_ids<Atoms>();
        constexpr auto arities = atom_arities<Atoms>();
        constexpr size_t source_arity = arities[S];
        constexpr size_t arity = arities[I];
        constexpr auto var_positions = invert<NumVars>(make_order<Atoms>(S));
        bool forward = true;
        for (size_t col = 1; col < arity; col++)
            if (ids[I][col] < 0 ||
                var_positions[ids[I][col]] <= var_positions[ids[I][col - 1]]) {
                forward = false;
                break;
            }
        if (!forward)
            return true;
        int max_pos = -1;
        for (size_t col = 0; col < arity; col++)
            if (ids[I][col] >= 0 && var_positions[ids[I][col]] > max_pos)
                max_pos = var_positions[ids[I][col]];
        return max_pos < (int)source_arity;
    }

    template <size_t S, size_t I>
    bool semijoin_check(const std::array<V, NumVars> &tuple,
                        const std::array<int, NumVars> &var_positions) const {
        if constexpr (atom_needs_semijoin_check<S, I>()) {
            constexpr auto ids = atom_ids<Atoms>();
            constexpr auto arities = atom_arities<Atoms>();
            constexpr size_t arity = arities[I];
            std::array<V, arity> key;
            for (size_t col = 0; col < arity; col++)
                key[col] = tuple[var_positions[ids[I][col]]];
            return std::get<I>(atoms).pred->stable_contains(key);
        }
        return true;
    }

    template <size_t F>
    bool filter_check(const std::array<V, NumVars> &tuple,
                      const std::array<int, NumVars> &var_positions) const {
        using Filt = std::tuple_element_t<F, Filters>;
        if constexpr (is_expression_filter<Filt>::value) {
            constexpr auto ids = expression_filter_var_ids_impl<Filt>::value;
            return [&]<size_t... Is>(std::index_sequence<Is...>) {
                return std::get<F>(filters).func(
                    tuple[var_positions[ids[Is]]]...);
            }(std::make_index_sequence<ids.size()>{});
        } else if constexpr (is_negated<Filt>::value) {
            constexpr auto ids = negated_var_ids<Filt>::value;
            constexpr size_t arity = negated_var_ids<Filt>::arity;
            std::array<V, arity> key;
            for (size_t i = 0; i < arity; i++)
                key[i] = tuple[var_positions[ids[i]]];
            return !std::get<F>(filters).pred->stable_contains(key);
        } else {
            constexpr int var_id_a = filter_vars<Filt>::a_id;
            constexpr int var_id_b = filter_vars<Filt>::b_id;
            static_assert(var_id_a >= 0 && var_id_b >= 0,
                          "filter variable not bound by a positive body atom");
            int pos_a = var_positions[var_id_a],
                pos_b = var_positions[var_id_b];
            constexpr Cmp op = filter_vars<Filt>::op;
            return cmp_apply<op>(tuple[pos_a], tuple[pos_b]);
        }
    }

    template <size_t S>
    auto
    make_residual_test(const std::array<int, NumVars> &var_positions) const {
        return
            [this, var_positions](const std::array<V, NumVars> &row) -> bool {
                bool passes = [&]<size_t... Is>(std::index_sequence<Is...>) {
                    return (semijoin_check<S, Is>(row, var_positions) && ...);
                }(std::make_index_sequence<NumAtoms>{});
                if (!passes)
                    return false;
                return [&]<size_t... Fs>(std::index_sequence<Fs...>) {
                    return (filter_check<Fs>(row, var_positions) && ...);
                }(std::make_index_sequence<std::tuple_size_v<Filters>>{});
            };
    }
};

} // namespace df::datalog
