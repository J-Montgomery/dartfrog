#pragma once

#include <array>
#include <span>
#include <tuple>
#include <type_traits>
#include <vector>

#include "dartfrog/leapers.hpp"
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
    constexpr size_t NA = std::tuple_size_v<Atoms>;
    std::array<std::array<int, MAX_ARITY>, NA> result{};
    for (auto &row : result)
        row.fill(-1);
    for_indices<NA>([&]<size_t I>() {
        using AT = atom_traits<std::tuple_element_t<I, Atoms>>;
        for (size_t j = 0; j < AT::arity; ++j)
            result[I][j] = AT::var_ids[j];
    });
    return result;
}

template <typename Atoms> constexpr auto atom_arities() {
    constexpr size_t NA = std::tuple_size_v<Atoms>;
    std::array<size_t, NA> result{};
    for_indices<NA>([&]<size_t I>() {
        result[I] = atom_traits<std::tuple_element_t<I, Atoms>>::arity;
    });
    return result;
}

template <typename Atoms> constexpr size_t num_vars() {
    constexpr auto ids = atom_ids<Atoms>();
    constexpr auto arities = atom_arities<Atoms>();
    int max_var_id = 0;
    for (size_t atom = 0; atom < ids.size(); ++atom)
        for (size_t col = 0; col < arities[atom]; ++col)
            if (ids[atom][col] > max_var_id)
                max_var_id = ids[atom][col];
    for (int var_id = 0; var_id <= max_var_id; ++var_id) {
        bool found = false;
        for (size_t atom = 0; atom < ids.size() && !found; ++atom)
            for (size_t col = 0; col < arities[atom] && !found; ++col)
                if (ids[atom][col] == var_id)
                    found = true;
        if (!found)
            throw std::logic_error("variable IDs must be contiguous from 0");
    }
    return (size_t)(max_var_id + 1);
}

template <size_t NV>
constexpr std::array<int, NV> invert(const std::array<int, NV> &order) {
    std::array<int, NV> result{};
    for (size_t i = 0; i < NV; ++i)
        result[order[i]] = (int)i;
    return result;
}

template <typename Atoms> constexpr auto make_order(size_t s) {
    constexpr size_t NV = num_vars<Atoms>();
    constexpr size_t NA = std::tuple_size_v<Atoms>;
    constexpr auto ids = atom_ids<Atoms>();
    constexpr auto arities = atom_arities<Atoms>();

    std::array<int, NV> order{};
    std::array<bool, NV> bound{};
    std::array<int, NV> score{};
    for (auto &flag : bound)
        flag = false;
    for (auto &sc : score)
        sc = 0;

    // bind a variable and update scores for its unbound neighbors
    auto bind = [&](int var_id) {
        bound[var_id] = true;
        for (size_t atom = 0; atom < NA; ++atom) {
            bool has_var = false;
            for (size_t col = 0; col < arities[atom]; ++col)
                if (ids[atom][col] == var_id) {
                    has_var = true;
                    break;
                }
            if (!has_var)
                continue;
            for (size_t col = 0; col < arities[atom]; ++col) {
                int neighbor = ids[atom][col];
                if (neighbor >= 0 && !bound[neighbor])
                    ++score[neighbor];
            }
        }
    };

    // Place the bound variables
    size_t num_bound = 0;
    for (size_t col = 0; col < arities[s]; ++col) {
        order[num_bound++] = ids[s][col];
        bind(ids[s][col]);
    }

    // bind the unbound variables in order of how often they
    // co-occur with already bound variables in this stratum
    while (num_bound < NV) {
        int best_var = -1, best_score = -1;
        for (int var_id = 0; var_id < (int)NV; ++var_id)
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
};

template <size_t NA> struct LevelPlan {
    std::array<ExtSpec, NA> entries{};
    size_t count = 0;
};

template <typename Atoms> constexpr auto level_plan(size_t s, size_t K) {
    constexpr size_t NA = std::tuple_size_v<Atoms>;
    constexpr size_t NV = num_vars<Atoms>();
    constexpr auto ids = atom_ids<Atoms>();
    constexpr auto arities = atom_arities<Atoms>();
    auto var_positions = invert<NV>(make_order<Atoms>(s));
    LevelPlan<NA> plan{};
    for (size_t atom = 0; atom < NA; ++atom) {
        if (atom == s)
            continue;
        size_t arity = arities[atom];
        int max_pos = -1;
        for (size_t col = 0; col < arity; ++col)
            if (ids[atom][col] >= 0 && var_positions[ids[atom][col]] > max_pos)
                max_pos = var_positions[ids[atom][col]];
        if (max_pos != (int)K)
            continue;
        bool forward = true;
        for (size_t col = 1; col < arity; ++col)
            if (ids[atom][col] < 0 || var_positions[ids[atom][col]] <=
                                          var_positions[ids[atom][col - 1]]) {
                forward = false;
                break;
            }
        if (forward)
            plan.entries[plan.count++] = {atom};
    }
    return plan;
}

template <typename T> struct is_negated : std::false_type {};
template <typename P, typename A, typename B>
struct is_negated<NegatedTerm<P, A, B>> : std::true_type {};

template <typename T> struct filter_vars;
template <typename P, int A, int B>
struct filter_vars<NegatedTerm<P, Var<A>, Var<B>>> {
    static constexpr int a_id = A;
    static constexpr int b_id = B;
};
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
        constexpr size_t NA = std::tuple_size_v<Atoms>;
        constexpr size_t NV = num_vars<Atoms>();
        constexpr auto ids = atom_ids<Atoms>();
        constexpr auto arities = atom_arities<Atoms>();
        constexpr auto var_positions = invert<NV>(make_order<Atoms>(S));
        for (size_t atom = 0; atom < NA; ++atom) {
            if (atom == S)
                continue;
            size_t arity = arities[atom];

            for (size_t col = 0; col < arity; ++col)
                for (size_t d = col + 1; d < arity; ++d)
                    if (ids[atom][col] == ids[atom][d])
                        return true;

            bool forward = true;
            for (size_t col = 1; col < arity; ++col)
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
            for (size_t col = 0; col < arity; ++col)
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
        for (size_t i = 0; i < K; ++i)
            result[i] = prefix[i];
        result[K] = new_val;
        return result;
    }
};

template <size_t Atom, typename V, size_t K, typename Atoms, size_t NV>
auto make_ext(const Atoms &atoms, const std::array<int, NV> &var_positions) {
    constexpr auto ids = atom_ids<Atoms>();
    constexpr auto arities = atom_arities<Atoms>();
    constexpr size_t N = arities[Atom];
    constexpr size_t ProposeCol = N - 1;

    std::array<int, ProposeCol> key_positions{};
    for (size_t col = 0; col < ProposeCol; ++col)
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
    constexpr size_t NV = num_vars<Atoms>();
    constexpr auto var_positions = invert<NV>(make_order<Atoms>(S));
    return std::make_tuple(
        make_ext<plan.entries[Js].atom, V, K>(atoms, var_positions)...);
}

template <typename V, size_t NV, size_t S, size_t K, typename Atoms>
df::Relation<std::array<V, NV>> extend(df::Relation<std::array<V, K>> prefix,
                                       const Atoms &atoms) {
    if constexpr (K == NV) {
        return std::move(prefix);
    } else {
        constexpr auto plan = level_plan<Atoms>(S, K);
        auto extractors = build_exts<V, K, S, K, Atoms>(
            atoms, std::make_index_sequence<plan.count>{});
        auto leapers = to_coll<V, K>(std::move(extractors));
        auto next =
            df::leapjoin(std::span<const std::array<V, K>>(prefix.elements),
                         leapers, ArrayAppender<V, K>{});
        return extend<V, NV, S, K + 1>(std::move(next), atoms);
    }
}

template <typename V, size_t NV, size_t S, size_t K, typename Atoms>
df::Relation<std::array<V, NV>> extend(std::span<const std::array<V, K>> src,
                                       const Atoms &atoms) {
    if constexpr (K == NV) {

        return df::Relation<std::array<V, NV>>{
            std::vector<std::array<V, NV>>(src.begin(), src.end())};
    } else {
        constexpr auto plan = level_plan<Atoms>(S, K);
        auto extractors = build_exts<V, K, S, K, Atoms>(
            atoms, std::make_index_sequence<plan.count>{});
        auto leapers = to_coll<V, K>(std::move(extractors));
        auto next = df::leapjoin(src, leapers, ArrayAppender<V, K>{});
        return extend<V, NV, S, K + 1>(std::move(next), atoms);
    }
}

template <typename HeadTerm, typename Atoms, typename Filters>
struct QueryPlanner {
    HeadTerm head;
    Atoms atoms;
    Filters filters;

    using V = typename atom_traits<
        std::tuple_element_t<0, Atoms>>::pred_t::TupleT::value_type;
    static constexpr size_t NV = num_vars<Atoms>();
    static constexpr size_t NA = std::tuple_size_v<Atoms>;

    template <size_t S> static constexpr bool source_is_forward_viable() {
        constexpr size_t source_arity = atom_arities<Atoms>()[S];
        for (size_t K = source_arity; K < NV; ++K)
            if (level_plan<Atoms>(S, K).count == 0)
                return false;
        return true;
    }

    void operator()() const {
        for_indices<NA>([&]<size_t S>() { do_source<S>(); });
    }

    void eval_full() const {
        for_indices<NA>([&]<size_t S>() { do_source_full<S>(); });
    }

    template <size_t S>
    void do_source_impl(
        std::span<const std::array<V, atom_arities<Atoms>()[S]>> src) const {
        constexpr size_t source_arity = atom_arities<Atoms>()[S];
        if (src.empty())
            return;
        auto joined_tuples = extend<V, NV, S, source_arity>(src, atoms);
        constexpr auto var_positions = invert<NV>(make_order<Atoms>(S));
        constexpr auto head_var_ids = atom_traits<HeadTerm>::var_ids;
        constexpr size_t head_arity = atom_traits<HeadTerm>::arity;
        constexpr auto head_positions =
            project<head_arity>(var_positions, head_var_ids);
        auto project_to_head = [&](const std::array<V, NV> &row) {
            return project<head_arity>(row, head_positions);
        };
        if constexpr (!has_residual_filters<S, Atoms, Filters>()) {
            head.pred->insert(df::Relation<std::array<V, head_arity>>::from_map(
                joined_tuples, project_to_head));
        } else {
            auto keep = make_residual_test<S>(var_positions);
            std::vector<std::array<V, head_arity>> result;
            result.reserve(joined_tuples.elements.size());
            for (const auto &row : joined_tuples.elements)
                if (keep(row))
                    result.push_back(project_to_head(row));
            head.pred->insert(df::Relation<std::array<V, head_arity>>::from_vec(
                std::move(result)));
        }
    }

    template <size_t S> void do_source() const {
        if constexpr (source_is_forward_viable<S>()) {
            auto *source_pred = std::get<S>(atoms).pred;
            do_source_impl<S>(source_pred->var.recent());
        }
    }

    // When starting a new stratum we need to iterate over all
    // the committed facts recorded in var.stable
    template <size_t S> void do_source_full() const {
        constexpr size_t source_arity = atom_arities<Atoms>()[S];
        if constexpr (source_is_forward_viable<S>()) {
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
        constexpr auto var_positions = invert<NV>(make_order<Atoms>(S));
        bool forward = true;
        for (size_t col = 1; col < arity; ++col)
            if (ids[I][col] < 0 ||
                var_positions[ids[I][col]] <= var_positions[ids[I][col - 1]]) {
                forward = false;
                break;
            }
        if (!forward)
            return true;
        int max_pos = -1;
        for (size_t col = 0; col < arity; ++col)
            if (ids[I][col] >= 0 && var_positions[ids[I][col]] > max_pos)
                max_pos = var_positions[ids[I][col]];
        return max_pos < (int)source_arity;
    }

    template <size_t S, size_t I>
    bool semijoin_check(const std::array<V, NV> &tuple,
                        const std::array<int, NV> &var_positions) const {
        if constexpr (atom_needs_semijoin_check<S, I>()) {
            constexpr auto ids = atom_ids<Atoms>();
            constexpr auto arities = atom_arities<Atoms>();
            constexpr size_t arity = arities[I];
            std::array<V, arity> key;
            for (size_t col = 0; col < arity; ++col)
                key[col] = tuple[var_positions[ids[I][col]]];
            return std::get<I>(atoms).pred->stable_contains(key);
        }
        return true;
    }

    template <size_t F>
    bool filter_check(const std::array<V, NV> &tuple,
                      const std::array<int, NV> &var_positions) const {
        using Filt = std::tuple_element_t<F, Filters>;
        constexpr int var_id_a = filter_vars<Filt>::a_id;
        constexpr int var_id_b = filter_vars<Filt>::b_id;
        static_assert(var_id_a >= 0 && var_id_b >= 0,
                      "filter variable not bound by a positive body atom");
        int pos_a = var_positions[var_id_a], pos_b = var_positions[var_id_b];
        if constexpr (is_negated<Filt>::value) {
            return !std::get<F>(filters).pred->stable_contains(
                {tuple[pos_a], tuple[pos_b]});
        } else {
            constexpr Cmp op = filter_vars<Filt>::op;
            return cmp_apply<op>(tuple[pos_a], tuple[pos_b]);
        }
    }

    template <size_t S>
    auto make_residual_test(const std::array<int, NV> &var_positions) const {
        return [this, var_positions](const std::array<V, NV> &row) -> bool {
            bool passes = [&]<size_t... Is>(std::index_sequence<Is...>) {
                return (semijoin_check<S, Is>(row, var_positions) && ...);
            }(std::make_index_sequence<NA>{});
            if (!passes)
                return false;
            return [&]<size_t... Fs>(std::index_sequence<Fs...>) {
                return (filter_check<Fs>(row, var_positions) && ...);
            }(std::make_index_sequence<std::tuple_size_v<Filters>>{});
        };
    }
};

} // namespace df::datalog
