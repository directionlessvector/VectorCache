#pragma once

#include <string>
#include <mutex>
#include <thread>
#include <atomic>

#include "sharded_store.h"
#include "aof.h"

// Thread-per-connection TCP server speaking RESP2. Concurrency
// control is delegated entirely to ShardedStore -- see
// sharded_store.h for why sharding is done as N independent Store
// instances rather than fine-grained locks inside one big table.
class Server {
public:
    // aof_path: where the append-only log lives. policy: durability
    // tradeoff (see aof.h). num_shards: 1 reproduces the old
    // single-global-mutex behavior exactly (useful as a benchmark
    // baseline); the real default is much higher.
    Server(int port, const std::string& aof_path, FsyncPolicy policy, size_t num_shards);
    ~Server();

    // Binds, listens, and loops accepting connections forever
    // (blocking call — spawns a thread per accepted connection).
    void run();

private:
    int port_;
    int listen_fd_;

    ShardedStore store_;
    Aof aof_;

    std::thread sweep_thread_;
    std::atomic<bool> stop_sweep_{false};

    void handle_client(int client_fd);
    void run_periodic_sweep();

    // Executes one already-parsed command against the store and
    // returns the RESP-encoded reply. Also responsible for logging
    // write commands to the AOF (translating relative TTLs to
    // absolute ones first, per aof.h's replay-correctness note).
    std::string dispatch(const std::vector<std::string>& args);

    // Loads and replays the AOF file at startup, applying each
    // logged command directly to store_ (no AOF re-logging during
    // replay, obviously, or we'd duplicate every entry).
    void load_aof(const std::string& path);
};