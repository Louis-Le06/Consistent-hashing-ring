#pragma once
// A cluster of cache nodes. Each node is one LRUCache; the Router decides
// which node owns a key. Works with ModuloRouter and ConsistentHashRouter.
//
// Note: LRUCache and both routers take their arguments as `std::string&`
// (a non-const reference), so every call below first copies the argument into
// a local variable. That is why these methods are not const.
//
// The cluster also remembers every key it has seen. LRUCache exposes no size(),
// so this key set is what stats() and snapshot() are computed from.
#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>

#include "lru_cache.hpp"

template <typename Router>
class Cluster {
public:
    Cluster(std::size_t capacity_per_node, Router router = Router{})
        : capacity_(capacity_per_node), router_(std::move(router)) {}

    // ---------- nodes ----------
    // Returns false if the node already exists.
    bool add_node(const std::string& id) {
        if (nodes_.count(id)) return false;
        std::string copy = id;  // router takes std::string&
        router_.add_node(copy);
        nodes_.insert(id);
        caches_.emplace(id, LRUCache(capacity_));
        return true;
    }

    // Returns false if the node is unknown. Its cached data is lost, like a
    // crashed server.
    bool remove_node(const std::string& id) {
        if (!nodes_.count(id)) return false;
        std::string copy = id;
        router_.remove_node(copy);
        nodes_.erase(id);
        caches_.erase(id);
        return true;
    }

    const std::set<std::string>& nodes() const { return nodes_; }
    std::size_t node_count() const { return nodes_.size(); }
    bool empty() const { return nodes_.empty(); }

    // ---------- routing ----------
    // Throws std::runtime_error when the cluster has no node.
    std::string node_for(const std::string& key) {
        std::string copy = key;
        return router_.node_for(copy);
    }

    // ---------- cache ----------
    std::optional<std::string> get(const std::string& key) {
        std::string copy = key;
        return caches_.at(node_for(copy)).get(copy);
    }

    void put(const std::string& key, const std::string& value) {
        std::string key_copy = key, value_copy = value;
        caches_.at(node_for(key_copy)).put(key_copy, value_copy);
        keys_.insert(key);
    }

    // Read-through: returns the value, and stores it on a miss.
    // `was_hit` reports whether the key was already cached on its owner.
    std::string read_through(const std::string& key, bool& was_hit) {
        if (auto cached = get(key)) {
            was_hit = true;
            keys_.insert(key);
            return *cached;
        }
        was_hit = false;
        const std::string value = "value-of-" + key;
        put(key, value);
        return value;
    }

    // ---------- measurement ----------
    const std::set<std::string>& keys() const { return keys_; }
    std::size_t key_count() const { return keys_.size(); }

    // How many known keys each node currently owns (idle nodes included).
    std::map<std::string, std::size_t> distribution() {
        std::map<std::string, std::size_t> counts;
        for (const auto& id : nodes_) counts[id] = 0;
        for (const auto& key : keys_) ++counts[node_for(key)];
        return counts;
    }

    // Who owns each known key right now. Compare two snapshots to count moves.
    std::map<std::string, std::string> snapshot() {
        std::map<std::string, std::string> owners;
        for (const auto& key : keys_) owners[key] = node_for(key);
        return owners;
    }

    static std::size_t count_moved(const std::map<std::string, std::string>& before,
                                   const std::map<std::string, std::string>& after) {
        std::size_t moved = 0;
        for (const auto& [key, owner] : before) {
            auto it = after.find(key);
            if (it != after.end() && it->second != owner) ++moved;
        }
        return moved;
    }

private:
    std::size_t capacity_;
    Router router_;
    std::set<std::string> nodes_;
    std::set<std::string> keys_;
    std::unordered_map<std::string, LRUCache> caches_;
};
