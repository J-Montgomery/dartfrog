#pragma once

#include <concepts>
#include <cstddef>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

#include "dartfrog/relation.hpp"
#include "dartfrog/variable.hpp"

namespace dt {

template <typename... Variables> class Iteration {
    std::tuple<std::unique_ptr<Variables>...> variables;

    struct TupleConstructorTag {};

    template <typename Tuple, std::size_t... Is>
    constexpr Iteration(TupleConstructorTag, Tuple &&t,
                        std::index_sequence<Is...>)
        : variables(std::move(std::get<Is>(t))...) {}

  public:
    constexpr Iteration() = default;

    template <typename Tuple>
    constexpr Iteration(Tuple &&t)
        : Iteration(TupleConstructorTag{}, std::forward<Tuple>(t),
                    std::make_index_sequence<
                        std::tuple_size_v<std::decay_t<Tuple>>>{}) {}

    constexpr bool changed() {
        // The more obvious fold expression short-circuits, which is
        // incorrect in queries with antijoins
        bool any = false;
        for_indices<sizeof...(Variables)>(
            [&]<size_t I>() { any |= std::get<I>(variables)->changed(); });
        return any;
    }

    template <std::totally_ordered Tuple,
              typename ConcreteVariable = Variable<Tuple>>
    constexpr auto variable() && {
        auto new_var = std::make_unique<ConcreteVariable>();

        auto *stable_ptr = new_var.get();
        auto updated_tuple = std::tuple_cat(
            std::move(variables),
            std::tuple<std::unique_ptr<ConcreteVariable>>{std::move(new_var)});

        using NewIterationType = Iteration<Variables..., ConcreteVariable>;

        struct Result {
            NewIterationType iter;
            ConcreteVariable *var_ptr;
        };

        return Result{.iter = NewIterationType(std::move(updated_tuple)),
                      .var_ptr = stable_ptr};
    }

    template <std::totally_ordered Tuple,
              typename ConcreteVariable = Variable<Tuple>>
    constexpr auto variable_indistinct() && {
        auto new_var = std::make_unique<ConcreteVariable>();
        new_var->distinct = false;

        auto *stable_ptr = new_var.get();
        auto updated_tuple = std::tuple_cat(
            std::move(variables),
            std::tuple<std::unique_ptr<ConcreteVariable>>{std::move(new_var)});

        using NewIterationType = Iteration<Variables..., ConcreteVariable>;

        struct Result {
            NewIterationType iter;
            ConcreteVariable *var_ptr;
        };

        return Result{.iter = NewIterationType(std::move(updated_tuple)),
                      .var_ptr = stable_ptr};
    }

    template <typename... OtherVars> friend class Iteration;
};

} // namespace dt
