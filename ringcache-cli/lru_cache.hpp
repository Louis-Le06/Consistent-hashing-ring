#pragma once
// LRU cache in the LeetCode 146 style: get/put in O(1).
//   - std::list keeps entries from most recent (front) to least recent (back)
//   - unordered_map points each key at its node in the list
// Time:  O(1)
// Space: O(k), k is the capacity of cache
#include <cstddef>
#include <list>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

class LRUCache {
public:
    LRUCache(std::size_t capacity) : capacity_(capacity) {}

    // Returns the value and marks the key as most recently used.
    std::optional<std::string> get(std::string& key) {
        if (!map_.count(key)){
            return {};
        }
        auto value = map_[key]->second;
        update(key, value);
        return value;
    }

    void put(std::string& key, std::string& value) {
        if (capacity_ <= 0){
            return;
        }
        if (!map_.count(key) && items_.size() == capacity_){
            auto del = items_.front(); items_.pop_front();
            map_.erase(del.first); 
        }
        update(key, value);
    }

    

private:
    std::size_t capacity_;
    std::list<std::pair<std::string, std::string>> items_;
    std::unordered_map<std::string, std::list<std::pair<std::string, std::string>>::iterator> map_;
    
    void update(std::string& key, std::string& value){
        auto it = map_.find(key);
        if (it != map_.end()){
            items_.erase(it->second);
        }
        items_.emplace_back(key, value);
        map_[key] = prev(end(items_));
    }
};
