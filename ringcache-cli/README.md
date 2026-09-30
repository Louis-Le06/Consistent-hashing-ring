# simple-ringcache CLI

Cluster of LRU caches with two placement algorithms: consistent hashing (ring +
virtual nodes) and modulo hashing as a baseline.

## Build

    cmake -B build && cmake --build build      # smhasher ships with the project
    ./build/demo

or without CMake:

    g++ -std=c++17 -O2 -I. -Ismhasher/src main.cpp smhasher/src/MurmurHash3.cpp -o demo
    ./demo

## Start-up flags

    --algo=both|ring|modulo   run both clusters side by side (default both)
    --virtual-nodes=N      virtual nodes per node (default 150)
    --nodes=N              start with N nodes: node-0 .. node-(N-1)
    --keys=N               load N sample keys right away
    --capacity=N           LRU capacity per node (default 100000)

## Commands

    add-node <name>        add a node, then report how many keys moved
    remove-node <name>     remove a node, then report how many keys moved
    lookup <key>           which node owns the key (read-through)
    stats                  key distribution per node
    load <n>               generate n sample keys (user:0 ... user:n-1)
    nodes                  list the nodes
    help                   command list
    quit                   leave


## Example

    ./demo --nodes=4 --keys=10000
    > add-node node-4
      added node-4 (5 nodes)
      ring    keys moved: 1998/10000 (20.0%)
      modulo  keys moved: 7976/10000 (79.8%)

By default both algorithms run over the same keys, so `stats` prints one column
pair per algorithm and every node change reports both move counts. Use
`--algo=ring` or `--algo=modulo` to run just one.
