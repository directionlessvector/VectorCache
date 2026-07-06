#include "sharded_store.h"
#include <iostream>
#include <cassert>
#include <thread>
#include <vector>
#include <atomic>

int main() {
    // --- Basic correctness across many shards ---
    ShardedStore ss(32);

    for (int i = 0; i < 500; ++i) {
        ss.set_string("key" + std::to_string(i), "val" + std::to_string(i));
    }
    assert(ss.size() == 500);
    std::cout << "PASS: 500 keys spread across 32 shards, size() == 500\n";

    bool all_correct = true;
    for (int i = 0; i < 500; ++i) {
        auto v = ss.get_string("key" + std::to_string(i));
        if (!v || *v != "val" + std::to_string(i)) all_correct = false;
    }
    assert(all_correct);
    std::cout << "PASS: all 500 values read back correctly regardless of shard\n";

    // --- num_shards = 1 behaves identically to old single-store design ---
    ShardedStore single(1);
    single.set_string("a", "1");
    single.set_string("b", "2");
    assert(single.get_string("a") == "1");
    assert(single.get_string("b") == "2");
    assert(single.size() == 2);
    std::cout << "PASS: num_shards=1 works correctly (this IS the old global-lock baseline)\n";

    // --- TTL passthrough works through the shard layer ---
    ss.set_string("expiring", "soon");
    ss.expire("expiring", 100);
    long long t = ss.ttl("expiring");
    assert(t > 90 && t <= 100);
    ss.persist("expiring");
    assert(ss.ttl("expiring") == kTtlNoExpiry);
    std::cout << "PASS: TTL/expire/persist correctly pass through to the right shard\n";

    // --- Concurrent writes from many threads, disjoint keys, verify no corruption ---
    ShardedStore concurrent(16);
    const int num_threads = 50;
    const int keys_per_thread = 100;
    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&concurrent, t]() {
            for (int i = 0; i < keys_per_thread; ++i) {
                std::string key = "t" + std::to_string(t) + "_k" + std::to_string(i);
                concurrent.set_string(key, key + "_value");
            }
        });
    }
    for (auto& th : threads) th.join();

    assert(concurrent.size() == static_cast<size_t>(num_threads * keys_per_thread));
    bool concurrent_ok = true;
    for (int t = 0; t < num_threads; ++t) {
        for (int i = 0; i < keys_per_thread; ++i) {
            std::string key = "t" + std::to_string(t) + "_k" + std::to_string(i);
            auto v = concurrent.get_string(key);
            if (!v || *v != key + "_value") concurrent_ok = false;
        }
    }
    assert(concurrent_ok);
    std::cout << "PASS: " << num_threads << " threads x " << keys_per_thread
              << " keys concurrently, zero corruption, size == "
              << concurrent.size() << "\n";

    std::cout << "\nALL SHARDED STORE TESTS PASSED\n";
    return 0;
}