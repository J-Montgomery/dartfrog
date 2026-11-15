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
        : row_data(rows) {
        lower_bounds[0] = 0;
        upper_bounds[0] = row_data.size();
        positions[0] = 0;
    }

    __attribute__((always_inline)) size_t find_group_end(size_t d, size_t begin,
                                                         size_t limit) const {
        const V &value = row_data[begin][d];

        size_t lo = begin;
        size_t step = 1;

        while (lo + step < limit && row_data[lo + step][d] == value) {
            lo += step;
            step <<= 1;
        }

        size_t hi = std::min(limit, lo + step + 1);

        while (lo < hi) {
            const size_t mid = lo + (hi - lo) / 2;

            if (!(value < row_data[mid][d]))
                lo = mid + 1;
            else
                hi = mid;
        }

        return lo;
    }

    __attribute__((always_inline)) size_t depth() const { return depth_value; }
    // Check if no more keys remain at the current level.
    __attribute__((always_inline)) bool at_end() const {
        return positions[depth_value] >= upper_bounds[depth_value];
    }
    __attribute__((always_inline)) const V &key() const {
        return row_data[positions[depth_value]][depth_value];
    }

    __attribute__((always_inline)) void next() {
        const size_t d = depth_value;

        if (!group_end_validity[d]) {
            group_ends[d] = find_group_end(d, positions[d], upper_bounds[d]);
        }

        positions[d] = group_ends[d];
        group_end_validity[d] = false;
    }

    // Advance to the first key >= target at the current level
    __attribute__((always_inline)) void seek(const V &target) {
        const size_t d = depth_value;
        group_end_validity[d] = false;
        size_t lo = positions[d];
        const size_t hi = upper_bounds[d];

        if (lo >= hi || row_data[lo][d] >= target)
            return;

        size_t step = 1;
        while (lo + step < hi && row_data[lo + step][d] < target) {
            lo += step;
            step <<= 1;
        }

        size_t upper = std::min(hi, lo + step + 1);
        while (lo < upper) {
            const size_t mid = lo + (upper - lo) / 2;
            if (row_data[mid][d] < target)
                lo = mid + 1;
            else
                upper = mid;
        }
        positions[d] = lo;
    }

    __attribute__((always_inline)) void open() {
        const size_t d = depth_value;

        if (!group_end_validity[d]) {
            group_ends[d] = find_group_end(d, positions[d], upper_bounds[d]);
            group_end_validity[d] = true;
        }

        const size_t nextDepth = d + 1;
        lower_bounds[nextDepth] = positions[d];
        upper_bounds[nextDepth] = group_ends[d];
        positions[nextDepth] = positions[d];
        group_end_validity[nextDepth] = false;
        depth_value = nextDepth;
    }

    __attribute__((always_inline)) void up() { --depth_value; }

    // Reposition to the first key of the current level's sub-range
    __attribute__((always_inline)) void rewind() {
        positions[depth_value] = lower_bounds[depth_value];
        group_end_validity[depth_value] = false;
    }

  private:
    std::span<const std::array<V, N>> row_data;
    size_t depth_value = 0;

    std::array<size_t, N + 1> lower_bounds{};
    std::array<size_t, N + 1> upper_bounds{};
    std::array<size_t, N + 1> positions{};

    std::array<size_t, N + 1> group_ends{};
    std::array<bool, N + 1> group_end_validity{};
};

// A trie iterator presenting several sorted batches, so
// duplicate keys shared by several batches are visited once.
template <typename V, size_t N, size_t MaxBatches = 16>
class MergedTrieIterator {
  public:
    explicit MergedTrieIterator(
        const std::vector<std::span<const std::array<V, N>>> &batches)
        : num_batches(batches.size()) {

        if (batches.size() > MaxBatches)
            throw std::logic_error(
                "MergedTrieIterator batch capacity exceeded");
        for (size_t i = 0; i < num_batches; ++i) {
            batch_iters[i] = TrieIterator<V, N>(batches[i]);
            active_batches[i] = static_cast<uint8_t>(i);
        }
        level_counts[0] = static_cast<uint8_t>(num_batches);
        refresh();
    }

    __attribute__((always_inline)) size_t depth() const { return depth_value; }
    __attribute__((always_inline)) bool at_end() const {
        return end_by_depth[depth_value];
    }
    __attribute__((always_inline)) const V &key() const { return keys_by_depth[depth_value]; }

    __attribute__((always_inline)) void next() {
        const V current = keys_by_depth[depth_value];
        const uint8_t count = level_counts[depth_value];

        for (uint8_t i = 0; i < count; ++i) {
            const uint8_t b = active_batches[i];
            if (!batch_iters[b].at_end() && batch_iters[b].key() == current) {
                batch_iters[b].next();
            }
        }
        refresh();
    }

    __attribute__((always_inline)) void seek(const V &target) {
        const uint8_t count = level_counts[depth_value];
        for (uint8_t i = 0; i < count; ++i) {
            const uint8_t b = active_batches[i];
            if (!batch_iters[b].at_end()) {
                batch_iters[b].seek(target);
            }
        }
        refresh();
    }

    __attribute__((always_inline)) void open() {
        const V current = keys_by_depth[depth_value];
        const uint8_t count = level_counts[depth_value];

        uint8_t matched = 0;
        for (uint8_t i = 0; i < count; ++i) {
            const uint8_t b = active_batches[i];
            if (!batch_iters[b].at_end() && batch_iters[b].key() == current) {
                std::swap(active_batches[matched], active_batches[i]);
                batch_iters[b].open();
                matched++;
            }
        }

        level_counts[++depth_value] = matched;
        if (depth_value < N) {
            refresh();
        }
    }

    __attribute__((always_inline)) void up() {
        const uint8_t count = level_counts[depth_value];
        for (uint8_t i = 0; i < count; ++i) {
            batch_iters[active_batches[i]].up();
        }
        --depth_value;
    }

    __attribute__((always_inline)) void rewind() {
        const uint8_t count = level_counts[depth_value];
        for (uint8_t i = 0; i < count; ++i) {
            batch_iters[active_batches[i]].rewind();
        }
        refresh();
    }

  private:
    __attribute__((always_inline)) void refresh() {
        const uint8_t count = level_counts[depth_value];
        const V *best = nullptr;

        for (uint8_t i = 0; i < count; ++i) {
            const uint8_t b = active_batches[i];
            if (!batch_iters[b].at_end()) {
                const V &k = batch_iters[b].key();
                if (best == nullptr || k < *best) {
                    best = &k;
                }
            }
        }

        end_by_depth[depth_value] = (best == nullptr);
        if (best != nullptr) {
            keys_by_depth[depth_value] = *best;
        }
    }

    std::array<TrieIterator<V, N>, MaxBatches> batch_iters;
    std::array<uint8_t, MaxBatches> active_batches;
    size_t num_batches = 0;
    size_t depth_value = 0;

    std::array<uint8_t, N + 1> level_counts{};
    std::array<V, N + 1> keys_by_depth{};
    std::array<bool, N + 1> end_by_depth{};
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
          typename Tries, typename Emit, typename PrefixTest>
void bind_var(Tries &tries, std::array<V, NumVars> &assignment,
              const Emit &emit, const PrefixTest &keep) {
    if constexpr (Var == NumVars) {
        emit(assignment);
    } else if constexpr (std::tuple_element_t<Var, Levels>::size() == 0) {
        if(keep.template operator()<Var + 1>(assignment)) {
            bind_var<V, NumVars, Levels, Var + 1>(tries, assignment, emit, keep);
        }
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
                if (keep.template operator()<Var + 1>(assignment)) {
                    (std::get<Bs>(tries).open(), ...);
                    bind_var<V, NumVars, Levels, Var + 1>(tries, assignment, emit, keep);
                    (std::get<Bs>(tries).up(), ...);
                }
                (std::get<Bs>(tries).next(), ...);
            }
        }(std::tuple_element_t<Var, Levels>{});
    }
}

template <typename V, size_t NumVars, auto AtomVars, typename Tries,
          typename Emit, typename PrefixTest>
void triejoin(Tries &tries, const Emit &emit, const PrefixTest &keep) {
    std::array<V, NumVars> assignment{};
    if (keep.template operator()<0>(assignment))
        bind_var<V, NumVars, BinderLevels<AtomVars, NumVars>, 0>(
            tries, assignment, emit, keep);
}

template <typename V, size_t NumVars, auto AtomVars, typename Tries,
          typename Emit>
void triejoin(Tries &tries, const Emit &emit) {
    std::array<V, NumVars> assignment{};
    auto prefix_test = []<size_t>(const std::array<V, NumVars> &) { return true; };
    bind_var<V, NumVars, BinderLevels<AtomVars, NumVars>, 0>(tries, assignment,
                                                             emit, prefix_test);
}

} // namespace df::datalog::lftj
