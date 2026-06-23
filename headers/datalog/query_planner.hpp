#pragma once
#include <array>
#include <span>
#include <tuple>
#include <type_traits>
#include <vector>

#include "../leapers.hpp"
#include "../relation.hpp"
#include "var.hpp"

namespace df::datalog {

template <class...> struct type_list {};
template <class T, class L> struct contains : std::false_type {};
template <class T, class H, class... R>
struct contains<T, type_list<H, R...>>
    : std::conditional_t<std::is_same_v<T, H>, std::true_type,
                         contains<T, type_list<R...>>> {};

template <class T, class L> struct add_var_impl;
template <class T, class... E> struct add_var_impl<T, type_list<E...>> {
    using type = std::conditional_t<contains<T, type_list<E...>>::value,
                                    type_list<E...>, type_list<E..., T>>;
};
template <class T, class L>
using add_var_t =
    std::conditional_t<is_var_v<T>, typename add_var_impl<T, L>::type, L>;

template <class T> struct atom_traits;
template <class P, class A, class B> struct atom_traits<Term<P, A, B>> {
    using pred_t = P;
    using v1_t = A;
    using v2_t = B;
};

template <class L, class... Atoms> struct collect {
    using type = L;
};
template <class L, class A, class... R> struct collect<L, A, R...> {
    using s1 = add_var_t<typename atom_traits<A>::v1_t, L>;
    using s2 = add_var_t<typename atom_traits<A>::v2_t, s1>;
    using type = typename collect<s2, R...>::type;
};
template <class PosTuple> struct uvars;
template <class... A> struct uvars<std::tuple<A...>> {
    using type = typename collect<type_list<>, A...>::type;
};
template <class PosTuple> using uvars_t = typename uvars<PosTuple>::type;

template <class T, class L> struct index_of {
    static constexpr int value = -1;
};
template <class T, class H, class... R> struct index_of<T, type_list<H, R...>> {
    static constexpr int value =
        std::is_same_v<T, H> ? 0
                             : (index_of<T, type_list<R...>>::value < 0
                                    ? -1
                                    : 1 + index_of<T, type_list<R...>>::value);
};
template <class L> struct list_size;
template <class... E> struct list_size<type_list<E...>> {
    static constexpr size_t value = sizeof...(E);
};

template <class PosTuple> constexpr auto atom_ids() {
    using UV = uvars_t<PosTuple>;
    constexpr size_t NA = std::tuple_size_v<PosTuple>;
    std::array<std::array<int, 2>, NA> r{};
    [&]<size_t... I>(std::index_sequence<I...>) {
        ((r[I] = {index_of<typename atom_traits<
                               std::tuple_element_t<I, PosTuple>>::v1_t,
                           UV>::value,
                  index_of<typename atom_traits<
                               std::tuple_element_t<I, PosTuple>>::v2_t,
                           UV>::value}),
         ...);
    }(std::make_index_sequence<NA>{});
    return r;
}

template <size_t NV>
constexpr std::array<int, NV> invert(const std::array<int, NV> &order) {
    std::array<int, NV> pos{};
    for (size_t i = 0; i < NV; ++i)
        pos[order[i]] = (int)i;
    return pos;
}

template <class PosTuple> constexpr auto make_order(int s) {
    constexpr size_t NV = list_size<uvars_t<PosTuple>>::value;
    constexpr size_t NA = std::tuple_size_v<PosTuple>;
    constexpr auto ids = atom_ids<PosTuple>();
    std::array<int, NV> order{};
    std::array<bool, NV> bound{};
    for (auto &b : bound)
        b = false;
    order[0] = ids[s][0];
    order[1] = ids[s][1];
    bound[order[0]] = true;
    bound[order[1]] = true;
    size_t filled = 2;
    while (filled < NV) {
        int best = -1, bestc = -1;
        for (int v = 0; v < (int)NV; ++v) {
            if (bound[v])
                continue;
            int c = 0;
            for (size_t a = 0; a < NA; ++a) {
                int i1 = ids[a][0], i2 = ids[a][1];
                if (i1 == v && i2 >= 0 && bound[i2])
                    c++;
                else if (i2 == v && i1 >= 0 && bound[i1])
                    c++;
            }
            if (c > bestc) {
                bestc = c;
                best = v;
            }
        }
        order[filled++] = best;
        bound[best] = true;
    }
    return order;
}

struct ExtSpec {
    int atom;
    int key_pos;
    bool reverse;
};
template <size_t NA> struct LevelPlan {
    std::array<ExtSpec, NA> e{};
    int n = 0;
};

template <class PosTuple> constexpr auto level_plan(int s, int K) {
    constexpr size_t NA = std::tuple_size_v<PosTuple>;
    constexpr size_t NV = list_size<uvars_t<PosTuple>>::value;
    constexpr auto ids = atom_ids<PosTuple>();
    auto pos = invert<NV>(make_order<PosTuple>(s));
    LevelPlan<NA> lp{};
    for (size_t a = 0; a < NA; ++a) {
        if ((int)a == s)
            continue;
        int i1 = ids[a][0], i2 = ids[a][1];
        if (i1 < 0 || i2 < 0 || i1 == i2)
            continue;
        int p1 = pos[i1], p2 = pos[i2];
        int hi = p1 > p2 ? p1 : p2;
        if (hi != K)
            continue;
        if (p1 > p2)
            lp.e[lp.n++] = {(int)a, p2, true};
        else
            lp.e[lp.n++] = {(int)a, p1, false};
    }
    return lp;
}

template <typename V, size_t K> struct ArrayIndexer {
    int idx;
    constexpr V operator()(const std::array<V, K> &p) const { return p[idx]; }
};

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

template <int Atom, int Kp, bool Rev, class V, size_t K, class AtomsT>
auto make_ext(const AtomsT &atoms) {
    auto *pred = std::get<Atom>(atoms).pred;
    const df::Relation<std::pair<V, V>> *rel =
        Rev ? &pred->snap_rev : &pred->snap_fwd;
    df::RelationLeaper<V, V> lw{rel};
    return lw.template extend_with<std::array<V, K>>(ArrayIndexer<V, K>{Kp});
}

template <class V, size_t K, class... E> auto to_coll(std::tuple<E...> &&t) {
    return df::LeaperCollection<std::array<V, K>, V, E...>{std::move(t)};
}

template <class V, size_t K, int S, int Klvl, class AtomsT, size_t... J>
auto build_exts(const AtomsT &atoms, std::index_sequence<J...>) {
    constexpr auto lp = level_plan<AtomsT>(S, Klvl);
    return std::make_tuple(
        make_ext<lp.e[J].atom, lp.e[J].key_pos, lp.e[J].reverse, V, K>(
            atoms)...);
}

template <class V, size_t NV, int S, size_t K, class AtomsT>
df::Relation<std::array<V, NV>> extend(df::Relation<std::array<V, K>> prefix,
                                       const AtomsT &atoms) {
    if constexpr (K == NV) {
        return std::move(prefix);
    } else {
        constexpr auto lp = level_plan<AtomsT>(S, (int)K);
        auto exts = build_exts<V, K, S, (int)K, AtomsT>(
            atoms, std::make_index_sequence<lp.n>{});
        auto coll = to_coll<V, K>(std::move(exts));
        auto next =
            df::leapjoin(std::span<const std::array<V, K>>(prefix.elements),
                         coll, [](const std::array<V, K> &p, const V &nv) {
                             std::array<V, K + 1> o;
                             for (size_t i = 0; i < K; ++i)
                                 o[i] = p[i];
                             o[K] = nv;
                             return o;
                         });
        return extend<V, NV, S, K + 1>(std::move(next), atoms);
    }
}

template <class T> struct is_negated : std::false_type {};
template <class P, class A, class B>
struct is_negated<NegatedTerm<P, A, B>> : std::true_type {};

template <class T> struct filter_vars;
template <class P, class A, class B> struct filter_vars<NegatedTerm<P, A, B>> {
    using a_t = A;
    using b_t = B;
};
template <Cmp Op, class A, class B> struct filter_vars<Compare<Op, A, B>> {
    using a_t = A;
    using b_t = B;
    static constexpr Cmp op = Op;
};

template <Cmp op, class T> constexpr bool cmp_apply(const T &x, const T &y) {
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

template <int S, class PosTuple, class FilterTuple>
constexpr bool has_residual_filters() {
    if constexpr (std::tuple_size_v<FilterTuple> > 0) {
        return true;
    } else {
        constexpr size_t NA = std::tuple_size_v<PosTuple>;
        constexpr size_t NV = list_size<uvars_t<PosTuple>>::value;
        constexpr auto ids = atom_ids<PosTuple>();
        constexpr auto pos = invert<NV>(make_order<PosTuple>(S));
        for (size_t a = 0; a < NA; ++a) {
            if ((int)a == S)
                continue;
            int i1 = ids[a][0], i2 = ids[a][1];
            if (i1 < 0 || i2 < 0)
                continue;
            if (i1 == i2)
                return true;
            if (std::max(pos[i1], pos[i2]) <= 1)
                return true;
        }
        return false;
    }
}

} // namespace df::datalog
