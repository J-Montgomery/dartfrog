#pragma once

#include "variable.hpp"
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <concepts>
#include <tuple>
#include <cstdint>

template <typename T>
concept VariableConcept = requires(T v) {
    { v.changed() } -> std::convertible_to<bool>;
};
template <typename... Variables>
class Iteration {
    std::tuple<Variables...> variables;

    struct TupleConstructorTag {};
    
    template <typename Tuple, std::size_t... Is>
    constexpr Iteration(TupleConstructorTag, Tuple&& t, std::index_sequence<Is...>)
        : variables(std::get<Is>(std::forward<Tuple>(t))...) {}

public:
    constexpr Iteration(Variables... vars) : variables(std::move(vars)...) {}

    template <typename Tuple>
    constexpr Iteration(Tuple&& t)
        : Iteration(TupleConstructorTag{}, std::forward<Tuple>(t), 
                    std::make_index_sequence<std::tuple_size_v<std::decay_t<Tuple>>>{}) {}

    constexpr bool changed() {
        return std::apply([](auto&&... vars) {
            return (vars.changed() || ...);
        }, variables);
    }

    template <std::totally_ordered Tuple, typename ConcreteVariable = Variable<Tuple>>
    constexpr auto variable() && {
        ConcreteVariable new_var;
        auto updated_tuple = std::tuple_cat(std::move(variables), std::tuple{std::move(new_var)});
        
        using NewIterationType = Iteration<Variables..., ConcreteVariable>;

        struct Result {
            NewIterationType iter;
            ConcreteVariable* var_ptr;
        };

        NewIterationType next_iter(std::move(updated_tuple));
        
        return Result{
            .iter = std::move(next_iter),
            .var_ptr = &std::get<sizeof...(Variables)>(next_iter.variables)
        };
    }

    template <std::totally_ordered Tuple, typename ConcreteVariable = Variable<Tuple>>
    constexpr auto variable_indistinct() && {
        ConcreteVariable new_var;
        new_var.distinct = false;
        auto updated_tuple = std::tuple_cat(std::move(variables), std::tuple{std::move(new_var)});
        
        using NewIterationType = Iteration<Variables..., ConcreteVariable>;

        struct Result {
            NewIterationType iter;
            ConcreteVariable* var_ptr;
        };

        NewIterationType next_iter(std::move(updated_tuple));

        return Result{
            .iter = std::move(next_iter),
            .var_ptr = &std::get<sizeof...(Variables)>(next_iter.variables)
        };
    }

    template <typename... OtherVars> friend class Iteration;
};