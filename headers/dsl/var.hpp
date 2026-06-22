#pragma once

#include <algorithm>
#include <tuple>

namespace detail {

template <size_t N> struct StringLiteral {
    constexpr StringLiteral(const char (&str)[N]) {
        std::copy_n(str, N, value);
    }
    char value[N];
};

} // namespace detail

template <detail::StringLiteral Name> struct Var {
    constexpr auto name() const { return Name.value; }
};

template <typename Pred, typename V1, typename V2> struct Term;
template <typename Left, typename Right> struct JoinExpr;
template <typename Head, typename Body> struct Rule;

template <typename Pred, typename V1, typename V2> struct Term {
    Pred *pred;
    V1 v1;
    V2 v2;

    template <typename BodyT> auto operator<<=(const BodyT &body) const {
        return Rule<Term, BodyT>{*this, body};
    }
};

// Left && Right
template <typename LeftT, typename RightT> struct JoinExpr {
    LeftT left;
    RightT right;
};

// Head <<= Body
template <typename HeadT, typename BodyT> struct Rule {
    HeadT head;
    BodyT body;
};

template <class T> struct is_var : std::false_type {};
template <detail::StringLiteral N> struct is_var<Var<N>> : std::true_type {};
template <class T> inline constexpr bool is_var_v = is_var<T>::value;

template <typename Pred, typename V1, typename V2>
struct NegatedTerm { Pred *pred; V1 v1; V2 v2; };

template <typename Pred, typename V1, typename V2>
auto operator!(const Term<Pred, V1, V2> &t) {
    return NegatedTerm<Pred, V1, V2>{t.pred, t.v1, t.v2};
}

enum class Cmp { Lt, Le, Gt, Ge, Ne, Eq };
template <Cmp Op, typename A, typename B> struct Compare { A a; B b; };

template <detail::StringLiteral A, detail::StringLiteral B>
auto operator<(Var<A> a, Var<B> b){ return Compare<Cmp::Lt,Var<A>,Var<B>>{a,b}; }

template <detail::StringLiteral A, detail::StringLiteral B>
auto operator>(Var<A> a, Var<B> b){ return Compare<Cmp::Gt,Var<A>,Var<B>>{a,b}; }

template <detail::StringLiteral A, detail::StringLiteral B>
auto operator>=(Var<A> a, Var<B> b){ return Compare<Cmp::Ge,Var<A>,Var<B>>{a,b}; }

template <detail::StringLiteral A, detail::StringLiteral B>
auto operator<=(Var<A> a, Var<B> b){ return Compare<Cmp::Le,Var<A>,Var<B>>{a,b}; }

template <detail::StringLiteral A, detail::StringLiteral B>
auto operator!=(Var<A> a, Var<B> b){ return Compare<Cmp::Ne,Var<A>,Var<B>>{a,b}; }

template <detail::StringLiteral A, detail::StringLiteral B>
auto operator==(Var<A> a, Var<B> b){ return Compare<Cmp::Eq,Var<A>,Var<B>>{a,b}; }


template <typename PosTuple, typename FilterTuple>
struct Conjunction {
    PosTuple pos;
    FilterTuple filt;
};


template <class T> struct is_filter_atom : std::true_type {};
template <class P, class A, class B>
struct is_filter_atom<Term<P, A, B>> : std::false_type {};


template <class P1, class A1, class B1, class P2, class A2, class B2>
auto operator&&(const Term<P1, A1, B1> &l, const Term<P2, A2, B2> &r) {
    return Conjunction<std::tuple<Term<P1, A1, B1>, Term<P2, A2, B2>>,
                       std::tuple<>>{std::make_tuple(l, r), {}};
}

template <class P, class A, class B, class F>
    requires is_filter_atom<F>::value
auto operator&&(const Term<P, A, B> &l, const F &f) {
    return Conjunction<std::tuple<Term<P, A, B>>, std::tuple<F>>{
        std::make_tuple(l), std::make_tuple(f)};
}

template <class Pos, class Filt, class P, class A, class B>
auto operator&&(const Conjunction<Pos, Filt> &c, const Term<P, A, B> &r) {
    return Conjunction<decltype(std::tuple_cat(c.pos, std::make_tuple(r))), Filt>{
        std::tuple_cat(c.pos, std::make_tuple(r)), c.filt};
}

template <class Pos, class Filt, class F>
    requires is_filter_atom<F>::value
auto operator&&(const Conjunction<Pos, Filt> &c, const F &f) {
    return Conjunction<Pos, decltype(std::tuple_cat(c.filt, std::make_tuple(f)))>{
        c.pos, std::tuple_cat(c.filt, std::make_tuple(f))};
}
