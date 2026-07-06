// Adversarial stress test, meant to be run under ThreadSanitizer
// (-fsanitize=thread). Unlike sharded_store_test.cpp (which uses
// disjoint keys per thread, so different threads rarely touch the
// same shard), this test deliberately maximizes contention: a SMALL
// pool of keys, hit by MANY threads, doing a MIX of every operation
// simultaneously. If there's any race in ShardedStore or Store that
// disjoint-key testing wouldn't expose, this is designed to trigger
// it.
#include "sharded_store.h"
#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <random>
#include <atomic>

int main() {
    ShardedStore store(8); // few shards relative to thread count -> real contention

    const int num_keys = 5;      // small pool -> many threads per shard
    const int num_threads = 32;
    const int ops_per_thread = 2000;

    std::vector<std::string> keys;
    for (int i = 0; i < num_keys; ++i) keys.push_back("hotkey" + std::to_string(i));

    std::atomic<long long> total_ops{0};

    auto worker = [&](int thread_id) {
        std::mt19937 rng(static_cast<unsigned>(thread_id) * 7919u + 1);
        std::uniform_int_distribution<int> key_pick(0, num_keys - 1);
        std::uniform_int_distribution<int> op_pick(0, 5);

        for (int i = 0; i < ops_per_thread; ++i) {
            const std::string& key = keys[static_cast<size_t>(key_pick(rng))];
            int op = op_pick(rng);

            switch (op) {
                case 0:
                    store.set_string(key, "v" + std::to_string(thread_id) + "_" + std::to_string(i));
                    break;
                case 1:
                    store.get_string(key); // result intentionally ignored -- just exercising the path
                    break;
                case 2:
                    store.del(key);
                    break;
                case 3:
                    store.exists(key);
                    break;
                case 4:
                    store.expire(key, 60);
                    break;
                case 5:
                    store.ttl(key);
                    break;
            }
            total_ops.fetch_add(1, std::memory_order_relaxed);
        }
    };

    // A dedicated sweeper thread running concurrently with everything
    // above -- this is the scenario NOT covered by the earlier manual
    // tests: sweep_expired() racing against concurrent set/expire/del
    // on the exact keys it's scanning.
    std::atomic<bool> stop_sweeping{false};
    std::thread sweeper([&]() {
        while (!stop_sweeping) {
            store.sweep_expired();
        }
    });

    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back(worker, t);
    }
    for (auto& th : threads) th.join();

    stop_sweeping = true;
    sweeper.join();

    std::cout << "Completed " << total_ops.load() << " mixed ops across "
              << num_threads << " threads + a concurrent sweeper, "
              << num_keys << " hot keys across " << store.num_shards() << " shards.\n";
    std::cout << "Final store size: " << store.size() << " (no crash, no hang = structurally sound)\n";

    return 0;
}