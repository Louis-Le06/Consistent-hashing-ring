// ringcache CLI - an interactive shell over a cluster of LRU caches.
//
// By default it keeps TWO clusters side by side over the same keys, one routed
// by consistent hashing and one by modulo hashing, so every command shows both
// answers and the two can be compared directly.
//
// Commands:
//   add-node <name>      add a node to every cluster
//   remove-node <name>   remove a node (its cached data is lost)
//   lookup <key>         show which node owns the key (read-through)
//   stats                key distribution per node
//   load <n>             generate n sample keys (user:0 .. user:n-1)
//   nodes                list the nodes
//   help                 command list
//   quit                 leave
//
// Start-up flags:
//   --algo=both|ring|modulo   which clusters to run (default both)
//   --virtual-nodes=N         virtual nodes per node (ring, default 150)
//   --nodes=N                 start with N nodes named node-0 .. node-(N-1)
//   --keys=N                  load N sample keys right away
//   --capacity=N              LRU capacity per node (default 100000)
//
// After every add-node / remove-node the CLI reports how many known keys
// changed node, which is the number that separates the two algorithms.
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "cluster.hpp"
#include "consistent_hash_router.hpp"
#include "modulo_router.hpp"

namespace {

// ---------- one uniform handle over Cluster<ModuloRouter> and Cluster<ConsistentHashRouter> ----------
class AnyCluster {
public:
    virtual ~AnyCluster() = default;
    virtual const std::string& label() const = 0;
    virtual bool add_node(const std::string& id) = 0;
    virtual bool remove_node(const std::string& id) = 0;
    virtual std::size_t node_count() const = 0;
    virtual bool empty() const = 0;
    virtual const std::set<std::string>& nodes() const = 0;
    virtual std::string node_for(const std::string& key) = 0;
    virtual std::string read_through(const std::string& key, bool& was_hit) = 0;
    virtual std::size_t key_count() const = 0;
    virtual std::map<std::string, std::size_t> distribution() = 0;
    virtual std::map<std::string, std::string> snapshot() = 0;
    virtual std::size_t moved_since(const std::map<std::string, std::string>& before) = 0;
};

template <typename Router>
class ClusterOf final : public AnyCluster {
public:
    ClusterOf(std::string label, std::size_t capacity, Router router)
        : label_(std::move(label)), cluster_(capacity, std::move(router)) {}

    const std::string& label() const override { return label_; }
    bool add_node(const std::string& id) override { return cluster_.add_node(id); }
    bool remove_node(const std::string& id) override { return cluster_.remove_node(id); }
    std::size_t node_count() const override { return cluster_.node_count(); }
    bool empty() const override { return cluster_.empty(); }
    const std::set<std::string>& nodes() const override { return cluster_.nodes(); }
    std::string node_for(const std::string& key) override { return cluster_.node_for(key); }
    std::string read_through(const std::string& key, bool& was_hit) override {
        return cluster_.read_through(key, was_hit);
    }
    std::size_t key_count() const override { return cluster_.key_count(); }
    std::map<std::string, std::size_t> distribution() override { return cluster_.distribution(); }
    std::map<std::string, std::string> snapshot() override { return cluster_.snapshot(); }
    std::size_t moved_since(const std::map<std::string, std::string>& before) override {
        return Cluster<Router>::count_moved(before, cluster_.snapshot());
    }

private:
    std::string label_;
    Cluster<Router> cluster_;
};

using Clusters = std::vector<std::unique_ptr<AnyCluster>>;

struct Options {
    std::string algo = "both";
    int vnodes = 150;
    std::size_t nodes = 0;
    std::size_t keys = 0;
    std::size_t capacity = 100000;
};

// ---------- small helpers ----------
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string percent(std::size_t part, std::size_t total) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(1);
    os << (total ? 100.0 * static_cast<double>(part) / static_cast<double>(total) : 0.0) << '%';
    return os.str();
}

// 33412 -> "33,412"
std::string group_digits(std::size_t value) {
    std::string text = std::to_string(value);
    for (int i = static_cast<int>(text.size()) - 3; i > 0; i -= 3)
        text.insert(static_cast<std::size_t>(i), ",");
    return text;
}

// count / mean, printed the way the load bars are labelled: "1.002x"
std::string load_factor(std::size_t count, double mean) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(3)
       << (mean > 0.0 ? static_cast<double>(count) / mean : 0.0) << 'x';
    return os.str();
}

// A bar as wide as this node's share of the busiest node, '.' for the rest.
std::string bar(std::size_t count, std::size_t max_count, std::size_t width) {
    const std::size_t filled =
        max_count ? static_cast<std::size_t>(static_cast<double>(width) *
                                             static_cast<double>(count) /
                                             static_cast<double>(max_count))
                  : 0;
    return std::string(filled, '#') + std::string(width - filled, '.');
}

void print_help() {
    std::cout << "Commands:\n"
              << "  add-node <name>      add a node\n"
              << "  remove-node <name>   remove a node\n"
              << "  lookup <key>         which node owns this key\n"
              << "  stats                key distribution per node\n"
              << "  load <n>             generate n sample keys (user:0 ...)\n"
              << "  nodes                list the nodes\n"
              << "  help                 this list\n"
              << "  quit                 leave\n";
}

// ---------- commands ----------
// One block of load bars per algorithm:
//
//   ring
//     node-0  ##############################  33,412  1.002x
//
// The bar is this node's share of the busiest node, the number is its key
// count, and the factor is count / mean (1.000x means a perfectly even share).
void cmd_stats(Clusters& clusters) {
    if (clusters.front()->empty()) {
        std::cout << "  (no node in the cluster)\n";
        return;
    }
    constexpr std::size_t kBarWidth = 30;

    for (std::size_t c = 0; c < clusters.size(); ++c) {
        const auto counts = clusters[c]->distribution();
        const std::size_t total = clusters[c]->key_count();
        const std::size_t node_count = clusters[c]->node_count();
        const double mean =
            node_count ? static_cast<double>(total) / static_cast<double>(node_count) : 0.0;

        std::size_t max_count = 0, name_width = 4, number_width = 1;
        for (const auto& [id, count] : counts) {
            max_count = std::max(max_count, count);
            name_width = std::max(name_width, id.size());
        }
        number_width = group_digits(max_count).size();

        if (clusters.size() > 1) std::cout << "  " << clusters[c]->label() << '\n';
        for (const auto& [id, count] : counts) {
            std::cout << "  " << std::left << std::setw(static_cast<int>(name_width) + 2) << id
                      << bar(count, max_count, kBarWidth) << "  " << std::right
                      << std::setw(static_cast<int>(number_width)) << group_digits(count) << "  "
                      << load_factor(count, mean) << '\n';
        }
        std::cout << "  " << node_count << " nodes, " << group_digits(total) << " keys, max/mean "
                  << (mean > 0.0 ? load_factor(max_count, mean) : "-") << '\n';
        if (c + 1 < clusters.size()) std::cout << '\n';
    }
}

void change_node(Clusters& clusters, const std::string& name, bool adding) {
    std::vector<std::map<std::string, std::string>> before;
    for (auto& cluster : clusters) before.push_back(cluster->snapshot());

    for (std::size_t c = 0; c < clusters.size(); ++c) {
        const bool ok = adding ? clusters[c]->add_node(name) : clusters[c]->remove_node(name);
        if (!ok) {
            std::cout << "  " << (adding ? "node already exists: " : "unknown node: ") << name
                      << '\n';
            return;
        }
    }
    std::cout << "  " << (adding ? "added " : "removed ") << name << " ("
              << clusters.front()->node_count() << " nodes)\n";

    const std::size_t total = before.front().size();
    if (total == 0) {
        std::cout << "  (no key loaded yet, use \"load <n>\" to measure key movement)\n";
        return;
    }
    if (clusters.front()->empty()) {
        std::cout << "  " << total << " keys now have no node\n";
        return;
    }
    for (std::size_t c = 0; c < clusters.size(); ++c) {
        const std::size_t moved = clusters[c]->moved_since(before[c]);
        std::cout << "  " << std::left << std::setw(8) << clusters[c]->label() << std::right
                  << "keys moved: " << moved << "/" << total << " (" << percent(moved, total)
                  << ")\n";
    }
    const std::size_t larger = clusters.front()->node_count() + (adding ? 0 : 1);
    std::cout << "  ideal for consistent hashing: about " << percent(1, larger) << '\n';
}

void cmd_lookup(Clusters& clusters, const std::string& key) {
    if (clusters.front()->empty()) {
        std::cout << "  no node in the cluster, add one first\n";
        return;
    }
    for (auto& cluster : clusters) {
        bool hit = false;
        const std::string value = cluster->read_through(key, hit);
        std::cout << "  " << std::left << std::setw(8) << cluster->label() << std::right << key
                  << " -> " << cluster->node_for(key)
                  << (hit ? "  [cache hit]" : "  [cache miss, stored now]") << "  value=" << value
                  << '\n';
    }
}

void cmd_load(Clusters& clusters, std::size_t count) {
    if (clusters.front()->empty()) {
        std::cout << "  no node in the cluster, add one first\n";
        return;
    }
    bool hit = false;
    for (std::size_t i = 0; i < count; ++i) {
        const std::string key = "user:" + std::to_string(i);
        for (auto& cluster : clusters) cluster->read_through(key, hit);
    }
    std::cout << "  loaded " << count << " keys, each cluster now knows "
              << clusters.front()->key_count() << " keys\n";
}

void cmd_nodes(Clusters& clusters) {
    if (clusters.front()->empty()) {
        std::cout << "  (no node)\n";
        return;
    }
    std::cout << " ";
    for (const auto& id : clusters.front()->nodes()) std::cout << ' ' << id;
    std::cout << '\n';
}

// ---------- the shell ----------
int run(const Options& opt, Clusters& clusters) {
    std::cout << "ringcache - algo=" << opt.algo;
    if (opt.algo != "modulo") std::cout << " virtual-nodes=" << opt.vnodes;
    std::cout << " capacity=" << opt.capacity << " per node\n";

    for (std::size_t i = 0; i < opt.nodes; ++i) {
        const std::string id = "node-" + std::to_string(i);
        for (auto& cluster : clusters) cluster->add_node(id);
    }
    if (opt.nodes) std::cout << "  started with " << clusters.front()->node_count() << " nodes\n";
    if (opt.keys) cmd_load(clusters, opt.keys);
    std::cout << "Type \"help\" for the command list.\n";

    std::string line;
    while (true) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line)) break;

        std::istringstream in(line);
        std::string command, argument;
        if (!(in >> command)) continue;
        in >> argument;
        command = lower(command);

        try {
            if (command == "quit" || command == "exit") {
                break;
            } else if (command == "help") {
                print_help();
            } else if (command == "add-node" || command == "remove-node") {
                if (argument.empty()) std::cout << "  usage: " << command << " <name>\n";
                else change_node(clusters, argument, command == "add-node");
            } else if (command == "lookup") {
                if (argument.empty()) std::cout << "  usage: lookup <key>\n";
                else cmd_lookup(clusters, argument);
            } else if (command == "stats") {
                cmd_stats(clusters);
            } else if (command == "nodes") {
                cmd_nodes(clusters);
            } else if (command == "load") {
                const long long n = argument.empty() ? -1 : std::stoll(argument);
                if (n <= 0) std::cout << "  usage: load <n>, n > 0\n";
                else cmd_load(clusters, static_cast<std::size_t>(n));
            } else {
                std::cout << "  unknown command: " << command << " (type \"help\")\n";
            }
        } catch (const std::exception& e) {
            std::cout << "  error: " << e.what() << '\n';
        }
    }
    std::cout << "bye\n";
    return 0;
}

bool parse_flag(const std::string& arg, const std::string& name, std::string& value) {
    const std::string prefix = "--" + name + "=";
    if (arg.rfind(prefix, 0) != 0) return false;
    value = arg.substr(prefix.size());
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            std::string value;
            if (parse_flag(arg, "virtual-nodes", value)) {
                opt.vnodes = std::stoi(value);
                if (opt.vnodes <= 0) throw std::invalid_argument("virtual-nodes must be > 0");
            } else if (parse_flag(arg, "algo", value)) {
                opt.algo = lower(value);
                if (opt.algo != "ring" && opt.algo != "modulo" && opt.algo != "both")
                    throw std::invalid_argument("algo must be both, ring or modulo");
            } else if (parse_flag(arg, "nodes", value)) {
                opt.nodes = static_cast<std::size_t>(std::stoull(value));
            } else if (parse_flag(arg, "keys", value)) {
                opt.keys = static_cast<std::size_t>(std::stoull(value));
            } else if (parse_flag(arg, "capacity", value)) {
                opt.capacity = static_cast<std::size_t>(std::stoull(value));
                if (opt.capacity == 0) throw std::invalid_argument("capacity must be > 0");
            } else if (arg == "--help" || arg == "-h") {
                std::cout << "usage: demo [--algo=both|ring|modulo] [--virtual-nodes=N]"
                          << " [--nodes=N] [--keys=N] [--capacity=N]\n\n";
                print_help();
                return 0;
            } else {
                throw std::invalid_argument("unknown flag: " + arg);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\ntry --help\n";
        return 1;
    }

    Clusters clusters;
    if (opt.algo != "modulo")
        clusters.push_back(std::make_unique<ClusterOf<ConsistentHashRouter>>(
            "ring", opt.capacity, ConsistentHashRouter{opt.vnodes}));
    if (opt.algo != "ring")
        clusters.push_back(
            std::make_unique<ClusterOf<ModuloRouter>>("modulo", opt.capacity, ModuloRouter{}));
    return run(opt, clusters);
}