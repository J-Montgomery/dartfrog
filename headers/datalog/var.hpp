#pragma once

#include <tuple>
#include <type_traits>

namespace df::datalog {

template <int N> struct Var {
    static constexpr int id = N;
};

template <class T> struct is_var : std::false_type {};
template <int N> struct is_var<Var<N>> : std::true_type {};
template <class T> inline constexpr bool is_var_v = is_var<T>::value;

template <typename Pred, typename... Vars> struct Term;
template <typename Head, typename Body> struct Rule;

template <typename Pred, typename... Vars> struct Term {
    Pred *pred;

    template <typename BodyT> auto operator%=(const BodyT &body) const {
        return Rule<Term, BodyT>{*this, body};
    }
};

template <typename Head, typename Body> struct Rule {
    Head head;
    Body body;
};

template <typename Pred, typename... Vars> struct NegatedTerm {
    Pred *pred;
};

template <typename Pred, typename... Vars>
auto operator!(const Term<Pred, Vars...> &t) {
    return NegatedTerm<Pred, Vars...>{t.pred};
}

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

template <class T> struct is_filter_atom : std::true_type {};
template <class P, class... Vars>
struct is_filter_atom<Term<P, Vars...>> : std::false_type {};

template <typename Func, int... VarIds> struct ExpressionFilter {
    Func func;
};

template <typename Func, int... VarIds>
struct is_filter_atom<ExpressionFilter<Func, VarIds...>> : std::true_type {};

template <int... VarIds, typename Func> auto where(Func f) {
    return ExpressionFilter<std::remove_cvref_t<Func>, VarIds...>{std::move(f)};
}

template <class P1, class... Vars1, class P2, class... Vars2>
auto operator&&(const Term<P1, Vars1...> &l, const Term<P2, Vars2...> &r) {
    return Conjunction<std::tuple<Term<P1, Vars1...>, Term<P2, Vars2...>>,
                       std::tuple<>>{std::make_tuple(l, r), {}};
}

template <class P, class... Vars, class F>
    requires is_filter_atom<F>::value
auto operator&&(const Term<P, Vars...> &l, const F &f) {
    return Conjunction<std::tuple<Term<P, Vars...>>, std::tuple<F>>{
        std::make_tuple(l), std::make_tuple(f)};
}

template <class Pos, class Filt, class P, class... Vars>
auto operator&&(const Conjunction<Pos, Filt> &c, const Term<P, Vars...> &r) {
    return Conjunction<decltype(std::tuple_cat(c.pos, std::make_tuple(r))),
                       Filt>{std::tuple_cat(c.pos, std::make_tuple(r)), c.filt};
}

template <class Pos, class Filt, class F>
    requires is_filter_atom<F>::value
auto operator&&(const Conjunction<Pos, Filt> &c, const F &f) {
    return Conjunction<Pos,
                       decltype(std::tuple_cat(c.filt, std::make_tuple(f)))>{
        c.pos, std::tuple_cat(c.filt, std::make_tuple(f))};
}

} // namespace df::datalog

// DL_VARS(a, b, c, ...) declares up to 8 named rule variables bound to
// Var<0>, Var<1>, ... in listing order, so a ported rule can name
// its variables instead of hand-numbering positional Var<N>:
//
//   DL_VARS(x, y, z);
//   dl.add_rule(Foo(c, y) %= Bar(z, y) && Baz(x, z));
//
// To improve performance, list variables in the order they appear in columns
#define DT_DL_NARG(...) DT_DL_NARG_IMPL(__VA_ARGS__, DT_DL_RSEQ_N())
#define DT_DL_NARG_IMPL(...) DT_DL_ARG_N(__VA_ARGS__)
#define DT_DL_ARG_N(_1, _2, _3, _4, _5, _6, _7, _8, N, ...) N
#define DT_DL_RSEQ_N() 8, 7, 6, 5, 4, 3, 2, 1, 0
#define DL_VARS(...)                                                           \
    auto [__VA_ARGS__] = ::df::datalog::Datalog::vars<DT_DL_NARG(__VA_ARGS__)>()
