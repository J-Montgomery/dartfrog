#pragma once

#include <algorithm>
#include <tuple>

namespace detail {

template<size_t N>
struct StringLiteral {
    constexpr StringLiteral(const char (&str)[N]) {
        std::copy_n(str, N, value);
    }
    char value[N];
};

} // namespace detail

template <detail::StringLiteral Name>
struct Var {
    constexpr auto name() const { return Name.value; }
};

template <typename Pred, typename V1, typename V2> struct Term;
template <typename Left, typename Right> struct JoinExpr;
template <typename Head, typename Body> struct Rule;

template <typename Pred, typename V1, typename V2>
struct Term {
    Pred* pred;
    V1 v1; V2 v2;

    template <typename BodyT>
    auto operator<<=(const BodyT& body) const {
        return Rule<Term, BodyT>{*this, body};
    }

    template <typename OtherTerm>
    auto operator&&(const OtherTerm& other) const {
        return JoinExpr<Term, OtherTerm>{*this, other};
    }
};

// Left && Right
template <typename LeftT, typename RightT>
struct JoinExpr {
    LeftT left;
    RightT right;
};

// Head <<= Body
template <typename HeadT, typename BodyT>
struct Rule {
    HeadT head;
    BodyT body;
};