#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace util {


class StringInterner {
 public:

    int32_t intern(std::string_view symbol) {
        if (const auto it = ids_.find(symbol); it != ids_.end()) {
            return it->second;
        }
        const int32_t id = static_cast<int32_t>(labels_.size());
        const auto [it, inserted] = ids_.emplace(std::string(symbol), id);
        labels_.emplace_back(it->first);
        return id;
    }

    std::optional<int32_t> lookup_id(std::string_view symbol) const {
        if (const auto it = ids_.find(symbol); it != ids_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    std::string_view label(int32_t id) const { return labels_[static_cast<size_t>(id)]; }

    size_t size() const { return labels_.size(); }

 private:
    struct Hash {
        using is_transparent = void;
        size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
    };
    struct Eq {
        using is_transparent = void;
        bool operator()(std::string_view a, std::string_view b) const { return a == b; }
    };

    std::unordered_map<std::string, int32_t, Hash, Eq> ids_;
    std::vector<std::string_view> labels_;
};

}  // namespace util
