// In-process benchmark: no sockets, no network stack, no syscalls
// per operation -- just many threads calling ShardedStore methods
// directly. This exists to isolate pure mutex-contention effects
// from the network/syscall overhead that dominates the end-to-end
// (client-over-TCP) benchmark in load_test.cpp.
//
// Usage: inprocess_bench <num_shards> <num_threads> <ops_per_thread>
#include "../src/sharded_store.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <string>

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "usage: " << argv[0] << " <num_shards> <num_threads> <ops_per_thread>\n";
        return 1;
    }

    size_t num_shards = static_cast<size_t>(std::atoi(argv[1]));
    int num_threads = std::atoi(argv[2]);
    int ops_per_thread = std::atoi(argv[3]);

    ShardedStore store(num_shards);

    // Pre-populate one key per thread so gets have something to read.
    for (int t = 0; t < num_threads; ++t) {
        store.set_string("key" + std::to_string(t), "value");
    }

    auto worker = [&store, ops_per_thread](int thread_id) {
        std::string key = "key" + std::to_string(thread_id);
        for (int i = 0; i < ops_per_thread; ++i) {
            if (i % 2 == 0) {
                store.set_string(key, "value");
            } else {
                store.get_string(key);
            }
        }
    };

    std::vector<std::thread> threads;
    auto start = std::chrono::steady_clock::now();
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back(worker, t);
    }
    for (auto& th : threads) th.join();
    auto end = std::chrono::steady_clock::now();

    double seconds = std::chrono::duration<double>(end - start).count();
    long long total_ops = static_cast<long long>(num_threads) * ops_per_thread;

    std::cout << "num_shards=" << num_shards
              << " threads=" << num_threads
              << " ops_per_thread=" << ops_per_thread
              << " total_ops=" << total_ops << "\n";
    std::cout << "total_time_sec=" << seconds << "\n";
    std::cout << "throughput_ops_per_sec=" << (static_cast<double>(total_ops) / seconds) << "\n";

    return 0;
}