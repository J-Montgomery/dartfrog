#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <numeric>
#include <span>
#include <vector>

namespace df::datalog::lftj {

// A trie iterator over a single lexicographically-sorted relation of N-column
// rows.
template <typename V, size_t N>
class TrieIterator {
 public:
    explicit TrieIterator(std::span<const std::array<V, N>> rows) : rows_(rows) {
        lo_[0] = 0;
        hi_[0] = rows_.size();
        pos_[0] = 0;
    }

    size_t depth() const { return depth_; }

    // Check if no more keys remain at the current level.
    bool at_end() const { return pos_[depth_] >= hi_[depth_]; }

    const V &key() const { return rows_[pos_[depth_]][depth_]; }

    void next() {
        const V current = key();
        size_t p = pos_[depth_];
        while (p < hi_[depth_] && rows_[p][depth_] == current) ++p;
        pos_[depth_] = p;
    }

    // Advance to the first key >= target at the current level (galloping search
    // bounded to the current sub-range). Leaves at_end() true if none.
    void seek(const V &target) {
        const size_t d = depth_;
        size_t lo = pos_[d];
        const size_t hi = hi_[d];

        // Gallop from lo to bracket target, then binary search the bracket.
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

    void open() {
        const V current = key();
        const size_t d = depth_;
        size_t end = pos_[d];
        while (end < hi_[d] && rows_[end][d] == current) ++end;
        lo_[d + 1] = pos_[d];
        hi_[d + 1] = end;
        pos_[d + 1] = pos_[d];
        ++depth_;
    }

    void up() { --depth_; }

    // Reposition to the first key of the current level's sub-range. Needed
    // before each leapfrog at a level so that an iterator whose current level
    // was consumed by a previous sibling branch restarts from the beginning.
    void rewind() { pos_[depth_] = lo_[depth_]; }

 private:
    std::span<const std::array<V, N>> rows_;
    size_t depth_ = 0;

    std::array<size_t, N + 1> lo_{};
    std::array<size_t, N + 1> hi_{};
    std::array<size_t, N + 1> pos_{};
};

// A trie iterator presenting several sorted batches, so
// duplicate keys shared by several batches are visited once.
template <typename V, size_t N>
class MergedTrieIterator {
 public:
    explicit MergedTrieIterator(const std::vector<std::span<const std::array<V, N>>> &batches) {
        for (const auto &b : batches) batches_.emplace_back(b);
        active_[0].resize(batches_.size());
        std::iota(active_[0].begin(), active_[0].end(), 0);
    }

    size_t depth() const { return depth_; }

    bool at_end() const {
        for (int b : active_[depth_])
            if (!batches_[b].at_end()) return false;
        return true;
    }

    const V &key() const {
        const V *best = nullptr;
        for (int b : active_[depth_]) {
            if (batches_[b].at_end()) continue;
            const V &k = batches_[b].key();
            if (best == nullptr || k < *best) best = &k;
        }
        return *best;
    }

    void next() {
        const V current = key();
        for (int b : active_[depth_])
            if (!batches_[b].at_end() && batches_[b].key() == current) batches_[b].next();
    }

    void seek(const V &target) {
        for (int b : active_[depth_])
            if (!batches_[b].at_end()) batches_[b].seek(target);
    }

    void open() {
        const V current = key();
        std::vector<int> &child = active_[depth_ + 1];
        child.clear();
        for (int b : active_[depth_]) {
            if (!batches_[b].at_end() && batches_[b].key() == current) {
                batches_[b].open();
                child.push_back(b);
            }
        }
        ++depth_;
    }

    void up() {
        for (int b : active_[depth_]) batches_[b].up();
        --depth_;
    }

    void rewind() {
        for (int b : active_[depth_]) batches_[b].rewind();
    }

 private:
    std::vector<TrieIterator<V, N>> batches_;
    size_t depth_ = 0;

    std::array<std::vector<int>, N + 1> active_{};
};

template <typename V, size_t N>
std::vector<std::array<V, N>> reindex_rows(const std::vector<std::array<V, N>> &rows,
                                           const std::array<int, N> &perm) {
    std::vector<std::array<V, N>> out;
    out.reserve(rows.size());
    for (const auto &row : rows) {
        std::array<V, N> t{};
        for (size_t i = 0; i < N; i++) t[i] = row[perm[i]];
        out.push_back(t);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

// Type-erased trie-iterator interface so a join can mix relations of different
// arities in one iterator list.
template <typename V>
struct AnyTrie {
    std::function<bool()> at_end;
    std::function<const V &()> key;
    std::function<void()> next;
    std::function<void(const V &)> seek;
    std::function<void()> open;
    std::function<void()> up;
    std::function<void()> rewind;
};

template <typename V, size_t N>
AnyTrie<V> erase_trie(TrieIterator<V, N> &it) {
    return AnyTrie<V>{[&] { return it.at_end(); },
                      [&]() -> const V & { return it.key(); },
                      [&] { it.next(); },
                      [&](const V &k) { it.seek(k); },
                      [&] { it.open(); },
                      [&] { it.up(); },
                      [&] { it.rewind(); }};
}

template <typename V, size_t N>
AnyTrie<V> erase_trie(MergedTrieIterator<V, N> &it) {
    return AnyTrie<V>{[&] { return it.at_end(); },
                      [&]() -> const V & { return it.key(); },
                      [&] { it.next(); },
                      [&](const V &k) { it.seek(k); },
                      [&] { it.open(); },
                      [&] { it.up(); },
                      [&] { it.rewind(); }};
}

template <typename V>
void leapfrog(std::vector<AnyTrie<V> *> &iters, const std::function<void(const V &)> &on_match) {
    for (AnyTrie<V> *it : iters)
        if (it->at_end()) return;

    std::sort(iters.begin(), iters.end(), [](AnyTrie<V> *a, AnyTrie<V> *b) {
        return a->key() < b->key();
    });
    size_t p = 0;
    const size_t k = iters.size();
    V high = iters[(p + k - 1) % k]->key();

    while (true) {
        const V low = iters[p]->key();
        if (low == high) {
            on_match(low);
            // Advance every iterator past the low point so the next
            // round makes progress.
            for (AnyTrie<V> *it : iters) {
                it->next();
                if (it->at_end()) return;
            }
            std::sort(iters.begin(), iters.end(), [](AnyTrie<V> *a, AnyTrie<V> *b) {
                return a->key() < b->key();
            });
            p = 0;
            high = iters[k - 1]->key();
        } else {
            iters[p]->seek(high);
            if (iters[p]->at_end()) return;
            high = iters[p]->key();
            p = (p + 1) % k;
        }
    }
}

template <typename V>
struct AtomPlan {
    AnyTrie<V> *trie;
    std::vector<int> vars;
};

// Depth-first triejoin over `num_vars` global variables bound in ascending order.
// Each atom's `vars` must also monotically increase.
template <typename V>
void triejoin(int num_vars,
              std::vector<AtomPlan<V>> &atoms,
              const std::function<void(const std::vector<V> &)> &emit) {
    std::vector<V> assignment(num_vars);

    std::vector<std::vector<AtomPlan<V> *>> binders(num_vars);
    for (auto &atom : atoms)
        if (!atom.vars.empty()) binders[atom.vars.front()].push_back(&atom);

    std::function<void(int)> recurse = [&](int var) {
        if (var == num_vars) {
            emit(assignment);
            return;
        }
        std::vector<AtomPlan<V> *> &here = binders[var];
        if (here.empty()) {
            recurse(var + 1);
            return;
        }
        std::vector<AnyTrie<V> *> its;
        its.reserve(here.size());
        for (AtomPlan<V> *a : here) its.push_back(a->trie);

        // Restart every participating iterator at the start of
        // its current sub-range so a cursor consumed by a previous sibling
        // branch at the parent level doesn't miss valid matches
        for (AnyTrie<V> *it : its) it->rewind();

        leapfrog<V>(its, [&](const V &val) {
            assignment[var] = val;
            // Descend every participating atom into this value.
            for (AtomPlan<V> *a : here) a->trie->open();
            // Register each advanced atom under the next variable it binds.
            std::vector<std::pair<int, AtomPlan<V> *>> readded;
            for (AtomPlan<V> *a : here) {
                auto pos = std::find(a->vars.begin(), a->vars.end(), var);
                auto nxt = std::next(pos);
                if (nxt != a->vars.end()) {
                    binders[*nxt].push_back(a);
                    readded.emplace_back(*nxt, a);
                }
            }

            recurse(var + 1);
            for (auto &[v, a] : readded) {
                auto &vec = binders[v];
                vec.erase(std::remove(vec.begin(), vec.end(), a), vec.end());
            }
            for (AtomPlan<V> *a : here) a->trie->up();
        });
    };

    recurse(0);
}

}  // namespace df::datalog::lftj
