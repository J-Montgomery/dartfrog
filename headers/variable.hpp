#pragma once

#include <concepts>
#include <cstdint>
#include <iostream>
#include <numeric>

template <typename T>
concept VariableTrait = requires(T v, uint32_t r, std::ostream &s) {
    { v.changed() } -> std::convertible_to<bool>;
    v.dump_stats(r, s);
};

struct IVariable {
    virtual ~IVariable() = default;
    virtual bool changed() = 0;
    virtual void dump_stats(uint32_t round, std::ostream &s) const = 0;
};

template <std::totally_ordered Tuple> class Variable : public IVariable {
  public:
    using TupleType = Tuple;

    bool distinct = true;
    std::string name_;

    std::vector<Relation<Tuple>> stable;
    Relation<Tuple> recent;
    std::vector<Relation<Tuple>> to_add;

    Variable(std::string name) : distinct(true), name_(std::move(name)) {}

    const std::string &name() const { return name_; }

    size_t num_stable() const {
        return std::accumulate(
            stable->begin(), stable->end(), 0ULL,
            [](size_t sum, const auto &rel) { return sum + rel.size(); });
    }

    bool is_stable() const { return recent->empty() && to_add->empty(); }

    void dump_stats(uint32_t round, std::ostream &w) const override {
        w << "\"" << name_ << "\", " << round << ", " << num_stable() << ", "
          << recent->size() << "\n";
    }
};
