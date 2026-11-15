#pragma once

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace df::datalog {

template <int N> struct Var {
    static_assert(N >= 0, "Var must be a non-negative index");
    static constexpr int Id = N;

    constexpr operator size_t() const noexcept {
        return static_cast<size_t>(N);
    }
};

template <class T> struct IsVar : std::false_type {};
template <int N> struct IsVar<Var<N>> : std::true_type {};
template <class T> inline constexpr bool IsVarV = IsVar<T>::value;

template <std::size_t N> static constexpr auto vars() {
    return []<std::size_t... Is>(std::index_sequence<Is...>) {
        return std::make_tuple(Var<static_cast<int>(Is)>{}...);
    }(std::make_index_sequence<N>{});
}

template <std::size_t N> inline constexpr auto VarsV = vars<N>();

template <typename T>
inline constexpr int VarIdV = std::remove_cvref_t<T>::Id;

template <typename Pred, typename... Vars> struct Term;
template <typename Head, typename Body> struct Rule;

template <typename Pred, typename... Vars> struct Term {
    Pred *pred;

    template <typename BodyT> auto operator<=(const BodyT &body) const {
        return Rule<Term, BodyT>{*this, body};
    }
};

template <typename Head, typename Body> struct Rule {
    Head head;
    Body body;
};

/* Multi-Head */

template <typename... Heads> struct MultiHead {
    std::tuple<Heads...> heads;

    template <typename BodyT> auto operator<=(const BodyT &body) const {
        return Rule<MultiHead, BodyT>{*this, body};
    }
};

template <typename P, typename... Vars>
auto heads_as_tuple(const Term<P, Vars...> &head) {
    return std::make_tuple(head);
}
template <typename... Heads>
auto heads_as_tuple(const MultiHead<Heads...> &head) {
    return head.heads;
}

// Yes, overriding the comma operator is awful and should never be done,
// but it looks so much better than the alternatives I've come up with
template <typename P1, typename... V1, typename P2, typename... V2>
auto operator,(const Term<P1, V1...> &a, const Term<P2, V2...> &b) {
    return MultiHead<Term<P1, V1...>, Term<P2, V2...>>{std::make_tuple(a, b)};
}
template <typename... Heads, typename P, typename... V>
auto operator,(const MultiHead<Heads...> &m, const Term<P, V...> &b) {
    return MultiHead<Heads..., Term<P, V...>>{
        std::tuple_cat(m.heads, std::make_tuple(b))};
}

/* Negation */

template <typename Pred, typename... Vars> struct NegatedTerm {
    Pred *pred;
};

template <typename Pred, typename... Vars>
auto operator!(const Term<Pred, Vars...> &t) {
    return NegatedTerm<Pred, Vars...>{t.pred};
}

/* Relational Operators */

enum class Cmp { Lt, Le, Gt, Ge, Ne, Eq };
template <Cmp Op, typename A, typename B> struct Compare {
    A a;
    B b;
};

template <int A, int B> auto operator<(Var<A> a, Var<B> b) {
    return Compare<Cmp::Lt, Var<A>, Var<B>>{a, b};
}
template <int A, int B> auto operator>(Var<A> a, Var<B> b) {
    return Compare<Cmp::Gt, Var<A>, Var<B>>{a, b};
}
template <int A, int B> auto operator>=(Var<A> a, Var<B> b) {
    return Compare<Cmp::Ge, Var<A>, Var<B>>{a, b};
}
template <int A, int B> auto operator<=(Var<A> a, Var<B> b) {
    return Compare<Cmp::Le, Var<A>, Var<B>>{a, b};
}
template <int A, int B> auto operator!=(Var<A> a, Var<B> b) {
    return Compare<Cmp::Ne, Var<A>, Var<B>>{a, b};
}
template <int A, int B> auto operator==(Var<A> a, Var<B> b) {
    return Compare<Cmp::Eq, Var<A>, Var<B>>{a, b};
}

template <typename PosTuple, typename FilterTuple> struct Conjunction {
    PosTuple pos;
    FilterTuple filt;
};

template <typename P, typename... Vars>
auto body_as_conjunction(const Term<P, Vars...> &body) {
    return Conjunction<std::tuple<Term<P, Vars...>>, std::tuple<>>{
        std::make_tuple(body), {}};
}
template <typename Pos, typename Filt>
const auto &body_as_conjunction(const Conjunction<Pos, Filt> &body) {
    return body;
}

template <class T> struct IsFilterAtom : std::true_type {};
template <class P, class... Vars>
struct IsFilterAtom<Term<P, Vars...>> : std::false_type {};

/* Expression filters */
template <typename Func, int... VarIds> struct ExpressionFilter {
    Func func;
};

template <typename Func, int... VarIds>
struct IsFilterAtom<ExpressionFilter<Func, VarIds...>> : std::true_type {};

template <auto... Vars, typename Func>
    requires(IsVarV<decltype(Vars)> && ...)
auto where(Func &&f) {
    using F = std::remove_cvref_t<Func>;
    return ExpressionFilter<F, VarIdV<decltype(Vars)>...>{
        std::forward<Func>(f)};
}

/* Aggregates */
template <typename Func, int OutVarIdT, int ValueVarIdT, int... GroupVarIdsT>
struct AggregateFilter {
    Func func;
    static constexpr int OutVarId = OutVarIdT;
    static constexpr int ValueVarId = ValueVarIdT;

    static constexpr std::array<int, sizeof...(GroupVarIdsT)> GroupVarIds{
        GroupVarIdsT...};
};

template <typename Func, int OutVarId, int ValueVarId, int... GroupVarIds>
struct IsFilterAtom<
    AggregateFilter<Func, OutVarId, ValueVarId, GroupVarIds...>>
    : std::true_type {};

template <auto OutVar, auto ValueVar, auto... GroupVars, typename Func>
    requires(IsVarV<decltype(OutVar)> && IsVarV<decltype(ValueVar)> &&
             (IsVarV<decltype(GroupVars)> && ...))
auto group_by(Func &&func) {
    using F = std::remove_cvref_t<Func>;

    return AggregateFilter<F, VarIdV<decltype(OutVar)>,
                           VarIdV<decltype(ValueVar)>,
                           VarIdV<decltype(GroupVars)>...>{
        std::forward<Func>(func)};
}

template <typename Func, int OutVarIdT, int... GroupVarIdsT>
struct AggregateRowsFilter {
    Func func;
    static constexpr int OutVarId = OutVarIdT;
    static constexpr int ValueVarId = -1;

    static constexpr std::array<int, sizeof...(GroupVarIdsT)> GroupVarIds{
        GroupVarIdsT...};
};

template <typename Func, int OutVarId, int... GroupVarIds>
struct IsFilterAtom<AggregateRowsFilter<Func, OutVarId, GroupVarIds...>>
    : std::true_type {};

template <auto OutVar, auto... GroupVars, typename Func>
    requires(IsVarV<decltype(OutVar)> &&
             (IsVarV<decltype(GroupVars)> && ...))
auto group_by_rows(Func &&func) {
    using F = std::remove_cvref_t<Func>;

    return AggregateRowsFilter<F, VarIdV<decltype(OutVar)>,
                               VarIdV<decltype(GroupVars)>...>{
        std::forward<Func>(func)};
}

/* Conjunctions of filters */

template <class Pos, class Filt, class Next>
auto append_body(const Conjunction<Pos, Filt> &body, const Next &next) {
    if constexpr (IsFilterAtom<Next>::value) {
        return Conjunction<Pos, decltype(std::tuple_cat(
                                    body.filt, std::make_tuple(next)))>{
            body.pos, std::tuple_cat(body.filt, std::make_tuple(next))};
    } else {
        return Conjunction<
            decltype(std::tuple_cat(body.pos, std::make_tuple(next))), Filt>{
            std::tuple_cat(body.pos, std::make_tuple(next)), body.filt};
    }
}

/* Rule conjunctions */

template <typename Head, typename Body, typename Next>
auto operator,(const Rule<Head, Body> &r, const Next &next) {
    const auto body = body_as_conjunction(r.body);
    auto appended = append_body(body, next);
    return Rule<Head, decltype(appended)>{r.head, std::move(appended)};
}

} // namespace df::datalog

// DL_VARS(a, b, c, ...) declares up to 16 named rule variables bound to
// Var<0>, Var<1>, ... in listing order, so a ported rule can name
// its variables instead of hand-numbering positional Var<N>:
//
//   {
//       DL_VARS(x, y, z);
//       dl.add_rule(Foo(c, y) <= Bar(z, y), Baz(x, z));
//    }
//
// To improve performance, list variables in the order they appear in columns
#define DT_DL_NARG(...) DT_DL_NARG_IMPL(__VA_ARGS__, DT_DL_RSEQ_N())
#define DT_DL_NARG_IMPL(...) DT_DL_ARG_N(__VA_ARGS__)
#define DT_DL_ARG_N(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13,    \
                    _14, _15, _16, N, ...)                                     \
    N
#define DT_DL_RSEQ_N() 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0
#define DL_VARS(...)                                                           \
    auto const &[__VA_ARGS__] = ::df::datalog::VarsV<DT_DL_NARG(__VA_ARGS__)>

#define DT_DL_UNPAREN(...) __VA_ARGS__
#define DT_DL_DECLARE_VARS(...)                                                \
    auto const &[__VA_ARGS__] =                                                \
        ::df::datalog::VarsV<DT_DL_NARG(__VA_ARGS__)>

// RULE registers a rule using the variables declared with a prior DL_VARS(...)
// In most cases, t
#define RULE(dl, ...) (dl).add_rule((__VA_ARGS__))

// DL_RULE registers a rule using inline variables
// This allows you to omit the scoping brackets usually required with DL_VARS
//
// DL_RULE(dl, (x, y, z), Foo(c, y) <= Bar(z, y), Baz(x, z));
//
#define DL_RULE(dl, vars, ...)                                                 \
    do {                                                                       \
        DT_DL_DECLARE_VARS(DT_DL_UNPAREN vars);                                \
        (dl).add_rule((__VA_ARGS__));                                          \
    } while (false)
