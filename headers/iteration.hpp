#pragma once

#include "variable.hpp"
#include <concepts>
#include <cstdint>
#include <memory>
#include <tuple>
#include <vector>

namespace df {

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
        // The more obvious fold expression short-circuits, which means
        // queries with antijoins can fail
        return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            bool results[] = {false, std::get<Is>(variables)->changed()...};
            bool any_changed = false;
            for (bool r : results)
                any_changed |= r;
            return any_changed;
        }(std::make_index_sequence<sizeof...(Variables)>{});
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

} // namespace df
