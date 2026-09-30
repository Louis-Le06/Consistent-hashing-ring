#pragma once
// Baseline: node = nodes[hash(key) % N].
// Changing N reassigns almost every key.
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

#include "hash.hpp"

class ModuloRouter {
public:
    void add_node(const std::string& id) {
        nodes_.push_back(id);
        std::sort(nodes_.begin(), nodes_.end());  
    }

    void remove_node(const std::string& id) {
        nodes_.erase(std::remove(nodes_.begin(), nodes_.end(), id), nodes_.end());
    }

    std::string& node_for(const std::string& key) {
        if (nodes_.empty()) throw std::runtime_error("no nodes");
        return nodes_[murmur64(key) % nodes_.size()];
    }

private:
    std::vector<std::string> nodes_;
};
