#pragma once

#include <vector>
#include <memory>
#include <iostream>
#include <string>
#include <cstdint>
#include "variable.hpp"

#include "variable.hpp"

class Iteration {
private:
    std::vector<std::shared_ptr<IVariable>> variables;
    uint32_t round = 0;
    std::ostream* debug_stats = nullptr;

public:
    Iteration() = default;

    bool changed() {
        round += 1;
        bool result = false;

        for (auto& variable: variables) {
            if(variable->changed()) {
                result = true;
            }

            if(debug_stats) {
                variable->dump_stats(round, *debug_stats);
            }
        }

        return result;
    }

    template <std::totally_ordered Tuple>
    std::shared_ptr<Variable<Tuple>> variable(std::string name) {
        auto var = std::make_shared<Variable<Tuple>>(std::move(name));
        variables.push_back(var);

        return var;
    }

    template <std::totally_ordered Tuple>
    std::shared_ptr<Variable<Tuple>> variable_indistinct(std::string name) {
        auto var = std::make_shared<Variable<Tuple>>(std::move(name));
        var->distinct = false;
        variables.push_back(var);

        return var;
    }

    void record_stats_to(std::ostream& s) {
        s << "Variable, Round, Stable Count, Recent Count\n";
        debug_stats = &s;
    }
};
