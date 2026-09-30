#pragma once
// Consistent hashing: every node owns `vnodes` points on a 64-bit ring.
// A key goes to the first point clockwise from hash(key), so adding or
// removing one node only moves about 1/N of the keys.
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>

#include "hash.hpp"

class ConsistentHashRouter {
public:
    explicit ConsistentHashRouter(int vnodes = 150) : vnodes_(vnodes) {}

    void add_node(std::string& id) {
        for (int i = 0; i < vnodes_; ++i) ring_[point(id, i)] = id;
    }

    void remove_node(std::string& id) {
        for (int i = 0; i < vnodes_; ++i) ring_.erase(point(id, i));
    }

    std::string& node_for(std::string& key) {
        if (ring_.empty()) throw std::runtime_error("no nodes");
        auto it = ring_.lower_bound(murmur64(key));  // first point >= hash
        if (it == ring_.end()) it = ring_.begin();   // wrap around
        return it->second;
    }

private:
    static std::uint64_t point(std::string& id, int i) {
        return murmur64(id + "#" + std::to_string(i));
    }

    int vnodes_;
    std::map<std::uint64_t, std::string> ring_;  // sorted => O(log n) lookup
};
