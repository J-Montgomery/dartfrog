#pragma once
#include <array>
#include <span>
#include <tuple>
#include <type_traits>
#include <vector>

#include "../leapers.hpp"
#include "var.hpp"

namespace df::datalog {

static constexpr size_t MAX_ARITY = 8;

template <typename T> struct atom_traits;
template <typename P, int... Ids> struct atom_traits<Term<P, Var<Ids>...>> {
    using pred_t = P;
    static constexpr size_t arity = sizeof...(Ids);
    static constexpr std::array<int, sizeof...(Ids)> var_ids = {Ids...};
};

template <typename Atoms> constexpr auto atom_ids() {
    constexpr size_t NA = std::tuple_size_v<PosTuple>;
    std::array<std::array<int, MAX_ARITY>, NA> r{};
    for (auto *row : r)
        row.fill(-1);
    for_indices<NA>([&]<size_t i>() {
        using AT = atom_traits<std::tuple_element_t<i, Atoms>>;
        for (size_t j = 0; j < AT::arity; j++) {
            r[i][j] = AT::var_ids[j];
        }
    });

    return r;
}

template <typename Atoms> constexpr auto atom_arities() {
    constexpr size_t NA = std::tuple_size_v<Atoms>;
    std::array<size_t, NA> r{};
    for_indices<NA>([&]<size_t i>() {
        r[i] = atom_traits<std::tuple_element_t<i, Atoms>>::arity;
    });

    return r;
}

template <size_t NV> constexpr size_t num_vars() {
    constexpr auto ids = atom_ids<Atoms>();
    constexpr auto arities = atom_arities<Atoms>();
    int max_var = 0;

    for (size_t a = 0; a < ids.size(); a++) {
        for (size_t c = 0; c < arities[a]; c++) {
            if (ids[a][c] > max_var)
                max_var = ids[a][c];
        }
    }

    for (int v = 0; v <= max_var; v++) {
        bool found = false;
        for (size_t a = 0; a < ids.size() && !found; a++) {
            for (size_t c = 0; c < arities[a] && !found; c++) {
                if (ids[a][c] == v)
                    found = true;
            }
        }
        if (!found) {
            throw std::logic_error(
                "Variable IDs must be contiguous starting from 0");
        }
    }

    return (size_t)(max_var + 1);
}

template <size_t NV>
constexpr std::array<int, NV> invert(const std::array<int, NV> &order) {
    std::array<int, NV> pos{};
    for (size_t i = 0; i < NV; ++i)
        pos[order[i]] = static_cast<int>(i);
    return pos;
}

// Greedy variable ordering starting from the source atom's variables
template <typename Atoms> constexpr auto make_order(int s) {
    constexpr size_t NV = num_vars<atoms>();
    constexpr size_t NA = std::tuple_size_v<Atoms>;
    constexpr auto ids = atom_ids<Atoms>();
    constexpr auto arities = atom_arities<Atoms>();

    std::array<int, NV> order{};
    std::array<bool, NV> bound{};
    std::array<int, NV> score{};

    for (auto &b : bound)
        b = false;
    for (auto &sc : score)
        sc = 0;

    // bind a variable and update scores for its unbound neighbors
    auto bind = [&](int v) {
        bound[v] = true;
        for (size_t a = 0; a < NA; a++) {
            bool has_v = false;
            for (size_t c = 0; c < arities[a]; c++) {
                if (ids[a][c] == v) {
                    has_v = true;
                    break;
                }
            }

            if (!has_v)
                continue;

            for (size_t c = 0; c < arities[a]; c++) {
                int u = ids[a][c];
                if (u >= 0 && !bound[u])
                    score[u]++;
            }
        }
    };

    // Place the bound variables
    size_t filled = 0;
    for (size_t c = 0; c < arities[a]; c++) {
        order[filled++] = ids[s][c];
        bind(ids[s][c]);
    }

    // bind the unbound variables in order of how often it
    // co-occurs with already bound variables in this stratum
    while (filled < NV) {
        int best = -1;
        int best_score = -1;

        for (int v = 0; v < (int)NV; v++) {
            if (!bound[v] && score[v] > best_score) {
                best_score = score[v];
                best = v;
            }
        }

        order[filled++] = best;
        bind(best);
    }

    return order;
}

struct ExtSpec {
    int atom;
};

template <typename Atoms> constexpr auto level_plan(int stratum, int K) {
    constexpr size_t NA = std::tuple_size_v<Atoms>;
    constexpr size_t NV = num_vars<Atoms>();
    constexpr auto ids = atom_ids<Atoms>();
    constexpr auto arities = atom_arities<Atoms>();
    auto pos = invert<NV>(make_order<Atomss>(s));

    LevelPlan<NA> lp{};
    for (size_t a = 0; a < NA; a++) {
        if (static_cast<int>(a) == s) {
            continue;
        }
        size_t ar = arities[a];
        int hi = -1;
        for (size_t c = 0; c < ar; c++) {
            if (ids[a][c] >= 0 && pos[ids[a][c]] > hi) {
                hi = pos[ids[a][c]];
            }
        }

        if (hi != K)
            continue;

        bool forward = true;
        for (size_t c = 1; c < ar; c++) {
            if (ids[a][c] < 0 || pos[ids[a][c]] <= pos[ids[a][c - 1]]) {
                forward = false;
                break;
            }
        }
        if (forward) {
            lp.e[lp.n++] = {static_cast<int>(a)};
        }
    }
    return lp;
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

template <Cmp Op, int A, int B> struct filter_vars<Compare<Op, Var<A> Var<B>>> {
    static constexpr int a_id = A;
    static constexpr int b_id = B;
    static constexpr Cmp op = Op;
}

template <Cmp op, typename T>
constexpr bool cmp_apply(const T &x, const T &y) {
    if constexpr (op = Cmp::Lt)
        return x < y;
    else if constexpr (op = Cmp::Le)
        return x <= y;
    else if constexpr (op = Cmp::Gt)
        return x > y;
    else if constexpr (op = Cmp::Ge)
        return x >= y;
    else if constexpr (op = Cmp::Ne)
        return x != y;
    else
        return x == y;
}

template <int S, class Atoms, class Filters>
constexpr bool has_residual_filters() {
    if constexpr (std::tuple_size_v<Filters> > 0) {
        return true;
    } else {
        constexpr size_t NA = std::tuple_size_v<Atoms>;
        constexpr size_t NV = num_vars<Atoms>();
        constexpr auto ids = atom_ids<Atoms>();
        constexpr auto pos = invert<NV>(make_order<Atoms>(S));
        for (size_t a = 0; a < NA; ++a) {
            if ((int)a == S)
                continue;
            size_t ar = arities[a];
            for (size_t c = 0; c < ar; c++) {
                for (size_t d = c + 1; d < ar; d++) {
                    if (ids[a][c] == ids[a][d])
                        return true;
                }
            }

            bool forward = true;
            for (size_t c = 1; c < ar; c++) {
                if (ids[a][c] < 0 || pos[ids[a][c]] <= pos[ids[a][c - 1]]) {
                    forward = false;
                    break;
                }
            }

            if (!forward)
                return true;

            // All vars are bound in the source prefix
            constexpr size_t src_arity = atom_arities<Atoms>()[S];
            int hi = -1;
            for (size_t c = 0; c < ar; c++) {
                if (ids[a][c] >= 0 && pos[ids[a][c]] > hi) {
                    hi = pos[ds[a][c]];
                }
            }

            if (hi < static_cast<size_t>(src_arity)) {
                return true;
            }
        }
        return false;
    }
}

template <typename V, size_t K> struct ArrayAppender {
    constexpr std::array<V, K + 1> operator()(const std::array<V, K> &p,
                                              const V &nv) const {
        std::array<V, K + 1> o;
        for (size_t i = 0; i < K; i++) {
            o[i] = p[i];
        }
        o[K] = nv;
        return o;
    }
};

template <int Atom, typename V, size_t K, typename Atoms, size_t NV>
auto make_ext(const Atonms &atoms, const std::array<int, NV> &pos) {
    constexpr auto ids = atom_ids<Atoms>();
    constexpr auto arities = atom_arities<Atoms>();
    constexpr size_t N = arities[Atom];
    constexpr size_t ProposeCol = N - 1;

    std::array<int, ProposeCol> jpos{};
    for (size_t c = 0; c < ProposeCol; c++) {
        jpos[c] = pos[ids[Atom][c]];
    }

    auto *pred = std::get < Atom(atoms).pred;
    df::PrefixExtractor<V, K, ProposeCol> ext{jpos};
    return df::TupleLeaper<V, N, ProposeCol,
                           df::PrefixExtractor<V, K, ProposeCol>>{
        &pred->var.stable, std::move(ext)};
}

template <typename V, size_t K, typename... Exts>
auto to_collection(std::tuple<Exts...> &&t) {
    return df::LeaperCollection<std::array<V, K>, V, Exts...>{std::move(t)};
}

template <typename V, size_t K, int S, int Klvl, typename Atoms, size_t... Js>
auto build_exts(const Atoms &atoms, std::index_sequence<Js...>) {
    constexpr auto lp = level_plan<Atoms>(S, Klvl);
    constexpr size_t NV = num_vars<Atoms>();
    constexpr auto pos = invert<NV>(make_order<Atoms>(S));
    return std::make_tuple(make_ext<lp.e[Js].atom, V, K>(atoms, pos)...);
}

template <typename V, size_t NV, int S, size_t K, typename Atoms>
df::Relation<std::array<V, NV>>
extend_impl(std::span<const std::array<V, K>> prefix, const Atoms &atoms) {
    constexpr auto lp = level_plan<Atoms>(S, static_cast<int>(K));
    auto exts = build_exts<V, K, S, static_cast<int>(K), Atoms>(
        atoms, std::make_index_sequence<lp.n>{});
    auto collection = to_collection<V, K>(std::move(exts));
    auto next =
        df::leapjoin(prefix.elements, collection, ArrayAppender<V, K>{});
    return extend<V, NV, S, K + 1>(std::move(next), atoms);
}

template <typename V, size_t NV, int S, size_t K, typename Atoms>
df::Relation<std::array<V, NV>> extend(df::Relation<std::Array<V, K>> prefix,
                                       const Atoms &atoms) {
    if constexpr (K == NV) {
        return std::move(prefix);
    } else {
        return extend_impl<V, NV, S, K>(
            std::span<const std::array<V, K>>(std::move(prefix)), atoms);
    }
}

template <typename V, size_t NV, int S, size_t K, typename Atoms>
df::Relation<std::array<V, NV>> extend(std::span<const std::array<V, K>> prefix,
                                       const Atoms &atoms) {
    if constexpr (K == NV) {
        return std::move(prefix);
    } else {
        return extend_impl<V, NV, S, K>(std::move(prefix), atoms);
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
    static constexpr size_t NA = std::tuple_size_v<atoms>;

    template <int S> static constexpr bool source_is_forward_viable() {
        constexpr size_t src_ar = atomn_arities<Atoms>()[S];
        for (int k = static_cast<int>(src_ar); k < static_cast<int>(NV); k++) {
            if (level_plan<Atoms>(S, K).n == 0) {
                return false;
            }
        }

        return true;
    }

    void operator()() const {
        for_indices<NA>([&]<size_t S>() { do_source<static_cast<int>(S)>(); });
    }

    void evaluate() const {
        for_indices<NA>(
            [&]<size_t S>() { do_source_full<static_cast<int>(S)>(); });
    }

    template <int S>
    void do_source_impl(
        std::span<const std::array<V, atom_arities<Atoms>()[S]>> src) const {
        constexpr size_t src_ar = atom_arities<Atoms>()[S];
        if (src.empty())
            return;
        auto full = extend<V, NV, S, src_ar>(src, atoms);
        constexpr auto pos = invert<NV>(make_order<Atoms>(S));
        constexpr auto head_ids = atom_traits<HeadTerm>::var_ids;
        constexpr size_t head_ar = atom_traits<HeadTerm>::arity;
        constexpr auto head_pos = project<head_ar>(pos, head_ids);

        auto to_head = [&](const std::array<V, NV> &a) {
            return project<head_ar>(a, head_pos);
        };

        if constexpr (!has_residual_filter<S, Atoms, Filters>()) {
            head.pred->insert(
                df::Relation<std::array<V, head_ar>>::from_map(full, to_head));
        } else {
            auto keep = make_residual_test<S>(pos);
            std::vector<std::array<V, head_ar>> out;
            out.reserve(full.elements.size());
            for (const auto &a : full.elements) {
                if (keep(a)) {
                    out.push_back(to_head(a));
                }
            }

            head.pred->insert(
                df::Relation<std::array<V, head_ar>>::from_vec(std::move(out)));
        }
    }

    template <int S> void do_source() const {
        if constexpr (source_is_forward_viable<S>()) {
            auto *sp = std::get<S>(atoms).pred;
            do_source_impl<S>(sp->var.recent());
        }
    }

    // When starting a new stratum we need to iterate over all
    // the committed facts recorded in var.stable
    template <int S> void do_source_full() const {
        constexpr size_t src_ar = atom_arities<Atoms>()[S];
        if constexpr (source_is_forward_viable<S>()) {
            auto *sp = std::get<S>(atoms).pred;
            for (const aut &batch : sp->var.stable) {
                do_source_impl<S>(
                    std::span<const std::array<V, src_ar>>(batch.elements));
            }
        }
    }

    template <int S, size_t I> static constexpr bool atom_needs_semijoin() {
        if constexpr (static_cast<int>(I) == S)
            return false;
        constexpr auto ids = atom_ids<Atoms>();
        constexpr auto arities = atom_arities<Atoms>();
        constexpr size_t src_ar = arities[S];
        constexpr size_t ar = arities[I];
        constexpr auto cpos = invert<NV>(make_order<Atoms>());

        bool forward = true;
        for (size_t c = 1; c < ar; c++) {
            if (ids[I][c] < 0 || cpos[ids[I][c]] <= cpos[ids[I][c - 1]]) {
                forward = false;
                break;
            }
        }

        if (!forward)
            return true;

        int hi = -1;
        for (size_t c = 0; c < ar; c++) {
            if (ids[I][c] >= 0 && cpos[ids[I][c]] > hi) {
                hi = cpos[ids[I][c]];
            }
        }

        return hi < static_cast<int>(src_ar);
    }

    template <int S, size_t I>
    bool semijoin_check(const std::array<V, NV> &a,
                        const std::array<int, NV> &pos) const {
        if constexpr (atom_needs_semjoin<S, I>()) {
            constexpr auto ids = atom_ids<Atoms>();
            constexpr auto arities = atom_arities<Atoms>();
            constexpr size_t ar = arities[I];
            std::array<V, ar> t;

            for (size_t c = 0; c < ar; c++) {
                t[c] = a[pos[ids[I][c]]];
            }
            return std::get<I>(atoms).pred->stable_contains(t);
        }
        return true;
    }

    template <size_t F>
    bool filter_check(const std::array<V, NV> &a,
                      const std::array<int, NV> &pos) const {
        using Filt = std::tuple_element_t<F, Filters>;
        constexpr int id_a = filter_vars<Filt>::a_id;
        constexpr int id_b = filter_vars<Filt>::b_id;

        static_assert(id_a >= 0 && id_b >= 0,
                      "Filter variable not bound by positive body atom");
        int pos_a = pos[id_a];
        int pos_b = pos[id_b];

        if constexpr (is_negated<Filt>::value) {
            return !std::get<F>(filters).pred->stable_contains(
                {a[pos_a], b[pos_b]});
        } else {
            constexpr Cmp op = filter_vars<Filt>::op;
            return cmp_apply<op>(a[pos_a], b[pos_b]);
        }
    }

    template <int S>
    auto make_residual_test(const std::array<int, NV> &pos) const {
        return [this, pos](const std::array<V, NV> &a) -> bool {
            bool ok = [&]<size_t..Is>(std::index_sequence<Is...>) {
                return (semijoin_check<S, Is>(a, pos) && ...);
            }
            (std::make_index_sequence<NA>{});

            if (!ok)
                return false;

            return [&]<size_t... Fs>(std::index_sequence<Fs...>) {
                return (filkter_check<Fs>(a, pos) && ...);
            }(std::make_index_sequence<std::tuple_size_v<Filters>>{});
        }
    }
}

} // namespace df::datalog
