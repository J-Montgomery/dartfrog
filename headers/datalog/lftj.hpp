#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <numeric>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

namespace df::datalog::lftj {

// A trie iterator over a single lexicographically-sorted relation of N-column
// rows.
template <typename V, size_t N> class TrieIterator {
  public:
    TrieIterator() = default;

    explicit TrieIterator(std::span<const std::array<V, N>> rows)
        : rows_(rows) {
        lo_[0] = 0;
        hi_[0] = rows_.size();
        pos_[0] = 0;
    }

    __attribute__((always_inline)) size_t depth() const { return depth_; }
    // Check if no more keys remain at the current level.
    __attribute__((always_inline)) bool at_end() const {
        return pos_[depth_] >= hi_[depth_];
    }
    __attribute__((always_inline)) const V &key() const {
        return rows_[pos_[depth_]][depth_];
    }

    __attribute__((always_inline)) void next() {
        const size_t d = depth_;

        if (group_end_valid_[d]) {
            pos_[d] = group_end_[d];
            group_end_valid_[d] = false;
            return;
        }

        const V current = key();
        size_t p = pos_[d];
        const size_t limit = hi_[d];

        while (p < limit && rows_[p][d] == current)
            ++p;

        pos_[d] = p;
    }

    // Advance to the first key >= target at the current level
    __attribute__((always_inline)) void seek(const V &target) {
        const size_t d = depth_;
        group_end_valid_[d] = false;
        size_t lo = pos_[d];
        const size_t hi = hi_[d];

        if (lo >= hi || rows_[lo][d] >= target)
            return;

        size_t step = 1;
        while (lo + step < hi && rows_[lo + step][d] < target) {
            lo += step;
            step <<= 1;
        }

        size_t upper = std::min(hi, lo + step + 1);
        while (lo < upper) {
            const size_t mid = lo + (upper - lo) / 2;
            if (rows_[mid][d] < target)
                lo = mid + 1;
            else
                upper = mid;
        }
        pos_[d] = lo;
    }

    __attribute__((always_inline)) void open() {
        const size_t d = depth_;
        const V current = key();

        size_t end;
        if (group_end_valid_[d]) {
            end = group_end_[d];
        } else {
            end = pos_[d];
            const size_t limit = hi_[d];

            while (end < limit && rows_[end][d] == current)
                ++end;

            group_end_[d] = end;
            group_end_valid_[d] = true;
        }

        const size_t next_d = d + 1;
        lo_[next_d] = pos_[d];
        hi_[next_d] = end;
        pos_[next_d] = pos_[d];
        group_end_valid_[next_d] = false;
        depth_ = next_d;
    }

    __attribute__((always_inline)) void up() { --depth_; }

    // Reposition to the first key of the current level's sub-range
    __attribute__((always_inline)) void rewind() {
        pos_[depth_] = lo_[depth_];
        group_end_valid_[depth_] = false;
    }

  private:
    std::span<const std::array<V, N>> rows_;
    size_t depth_ = 0;

    std::array<size_t, N + 1> lo_{};
    std::array<size_t, N + 1> hi_{};
    std::array<size_t, N + 1> pos_{};

    std::array<size_t, N + 1> group_end_{};
    std::array<bool, N + 1> group_end_valid_{};
};

// A trie iterator presenting several sorted batches, so
// duplicate keys shared by several batches are visited once.
template <typename V, size_t N, size_t MAX_BATCHES = 16>
class MergedTrieIterator {
  public:
    explicit MergedTrieIterator(
        const std::vector<std::span<const std::array<V, N>>> &batches)
        : num_batches_(batches.size()) {

        if (batches.size() > MAX_BATCHES)
            throw std::logic_error(
                "MergedTrieIterator batch capacity exceeded");
        for (size_t i = 0; i < num_batches_; ++i) {
            batches_[i] = TrieIterator<V, N>(batches[i]);
            active_[i] = static_cast<uint8_t>(i);
        }
        counts_[0] = static_cast<uint8_t>(num_batches_);
        refresh();
    }

    __attribute__((always_inline)) size_t depth() const { return depth_; }
    __attribute__((always_inline)) bool at_end() const {
        return at_end_[depth_];
    }
    __attribute__((always_inline)) const V &key() const { return key_[depth_]; }

    __attribute__((always_inline)) void next() {
        const V current = key_[depth_];
        const uint8_t count = counts_[depth_];

        for (uint8_t i = 0; i < count; ++i) {
            const uint8_t b = active_[i];
            if (!batches_[b].at_end() && batches_[b].key() == current) {
                batches_[b].next();
            }
        }
        refresh();
    }

    __attribute__((always_inline)) void seek(const V &target) {
        const uint8_t count = counts_[depth_];
        for (uint8_t i = 0; i < count; ++i) {
            const uint8_t b = active_[i];
            if (!batches_[b].at_end()) {
                batches_[b].seek(target);
            }
        }
        refresh();
    }

    __attribute__((always_inline)) void open() {
        const V current = key_[depth_];
        const uint8_t count = counts_[depth_];

        uint8_t matched = 0;
        for (uint8_t i = 0; i < count; ++i) {
            const uint8_t b = active_[i];
            if (!batches_[b].at_end() && batches_[b].key() == current) {
                std::swap(active_[matched], active_[i]);
                batches_[b].open();
                matched++;
            }
        }

        counts_[++depth_] = matched;
        if (depth_ < N) {
            refresh();
        }
    }

    __attribute__((always_inline)) void up() {
        const uint8_t count = counts_[depth_];
        for (uint8_t i = 0; i < count; ++i) {
            batches_[active_[i]].up();
        }
        --depth_;
    }

    __attribute__((always_inline)) void rewind() {
        const uint8_t count = counts_[depth_];
        for (uint8_t i = 0; i < count; ++i) {
            batches_[active_[i]].rewind();
        }
        refresh();
    }

  private:
    __attribute__((always_inline)) void refresh() {
        const uint8_t count = counts_[depth_];
        const V *best = nullptr;

        for (uint8_t i = 0; i < count; ++i) {
            const uint8_t b = active_[i];
            if (!batches_[b].at_end()) {
                const V &k = batches_[b].key();
                if (best == nullptr || k < *best) {
                    best = &k;
                }
            }
        }

        at_end_[depth_] = (best == nullptr);
        if (best != nullptr) {
            key_[depth_] = *best;
        }
    }

    std::array<TrieIterator<V, N>, MAX_BATCHES> batches_;
    std::array<uint8_t, MAX_BATCHES> active_;
    size_t num_batches_ = 0;
    size_t depth_ = 0;

    std::array<uint8_t, N + 1> counts_{};
    std::array<V, N + 1> key_{};
    std::array<bool, N + 1> at_end_{};
};

template <typename V, size_t N>
std::vector<std::array<V, N>>
reindex_rows(const std::vector<std::array<V, N>> &rows,
             const std::array<int, N> &perm) {
    std::vector<std::array<V, N>> out;
    out.reserve(rows.size());
    for (const auto &row : rows) {
        std::array<V, N> t{};
        for (size_t i = 0; i < N; i++)
            t[i] = row[perm[i]];
        out.push_back(t);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

// `AtomVars[atom][col]` is the join variable bound by that column of that atom,
// ascending across columns.
template <auto AtomVars, size_t Var> constexpr size_t count_binders() {
    size_t count = 0;
    for (const auto &vars : AtomVars)
        for (int var : vars)
            count += (var == static_cast<int>(Var));
    return count;
}

template <auto AtomVars, size_t Var> constexpr auto binders_of() {
    std::array<size_t, count_binders<AtomVars, Var>()> binders{};
    size_t count = 0;
    for (size_t atom = 0; atom < AtomVars.size(); atom++)
        for (int var : AtomVars[atom])
            if (var == static_cast<int>(Var))
                binders[count++] = atom;
    return binders;
}

template <auto Values, size_t... Is>
constexpr auto to_index_sequence(std::index_sequence<Is...>) {
    return std::index_sequence<Values[Is]...>{};
}

template <auto Values>
using IndexSequenceOf = decltype(to_index_sequence<Values>(
    std::make_index_sequence<Values.size()>{}));

template <auto AtomVars, size_t... Vars>
constexpr auto binder_levels(std::index_sequence<Vars...>) {
    return std::tuple<IndexSequenceOf<binders_of<AtomVars, Vars>()>...>{};
}

template <auto AtomVars, size_t NumVars>
using BinderLevels =
    decltype(binder_levels<AtomVars>(std::make_index_sequence<NumVars>{}));

template <typename V, size_t NumVars, typename Levels, size_t Var,
          typename Tries, typename Emit>
void bind_var(Tries &tries, std::array<V, NumVars> &assignment,
              const Emit &emit) {
    if constexpr (Var == NumVars) {
        emit(assignment);
    } else if constexpr (std::tuple_element_t<Var, Levels>::size() == 0) {
        bind_var<V, NumVars, Levels, Var + 1>(tries, assignment, emit);
    } else {
        [&]<size_t... Bs>(std::index_sequence<Bs...>) {
            // Restart every participating iterator at the start of its current
            // sub-range so a cursor consumed by a previous sibling branch at
            // the parent level doesn't miss valid matches.
            (std::get<Bs>(tries).rewind(), ...);
            while (!(std::get<Bs>(tries).at_end() || ...)) {
                // Leapfrog: everyone must reach the highest key on offer, so
                // seek the laggards up to it
                const std::array<V, sizeof...(Bs)> keys{
                    std::get<Bs>(tries).key()...};
                const V high = *std::max_element(keys.begin(), keys.end());
                if (std::any_of(keys.begin(), keys.end(),
                                [&](const V &k) { return k != high; })) {
                    (std::get<Bs>(tries).seek(high), ...);
                    continue;
                }
                assignment[Var] = high;
                (std::get<Bs>(tries).open(), ...);
                bind_var<V, NumVars, Levels, Var + 1>(tries, assignment, emit);
                (std::get<Bs>(tries).up(), ...);
                (std::get<Bs>(tries).next(), ...);
            }
        }(std::tuple_element_t<Var, Levels>{});
    }
}

template <typename V, size_t NumVars, auto AtomVars, typename Tries,
          typename Emit>
void triejoin(Tries &tries, const Emit &emit) {
    std::array<V, NumVars> assignment{};
    bind_var<V, NumVars, BinderLevels<AtomVars, NumVars>, 0>(tries, assignment,
                                                             emit);
}

} // namespace df::datalog::lftj
