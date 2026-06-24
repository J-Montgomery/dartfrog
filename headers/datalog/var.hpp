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

    template <typename BodyT> auto operator<<=(const BodyT &body) const {
        return Rule<Term, BodyT>{*this, body};
    }
};

template <typename Head, typename Body> struct Rule {
    Head head;
    Body body;
};

template <typename Pred, typename V1, typename V2> struct NegatedTerm {
    Pred *pred;
};

template <typename Pred, typename V1, typename V2>
auto operator!(const Term<Pred, V1, V2> &t) {
    return NegatedTerm<Pred, V1, V2>{t.pred};
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
