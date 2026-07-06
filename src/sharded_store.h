#pragma once

#include <vector>
#include <mutex>
#include <memory>
#include <string>
#include <utility>

#include "store.h"

// Wraps N independent Store instances ("shards"), each with its own
// mutex, routing each key to exactly one shard by hash. This is
// composition, not a rewrite of Store's internals: each shard is a
// complete, independent instance of the same hash table + TTL logic
// from Day 1/3, just responsible for a fraction of the keyspace.
//
// Why this design over fine-grained locking inside one big table:
// each shard resizes/rehashes independently. If we'd instead put many
// small locks inside ONE big hash table, a resize would still need
// to touch every bucket -- forcing a global lock during resize and
// defeating the point. With N independent Stores, shard A resizing
// never blocks shard B.
//
// num_shards = 1 is deliberately equivalent to the old single global
// mutex design -- same code path, so it doubles as the "before"
// baseline for benchmarking sharded vs. non-sharded without
// maintaining two separate implementations.
class ShardedStore {
public:
    explicit ShardedStore(size_t num_shards = 32);

    void set_string(const std::string& key, const std::string& value);
    std::optional<std::string> get_string(const std::string& key) const;
    bool del(const std::string& key);
    bool exists(const std::string& key) const;

    bool expire(const std::string& key, long long seconds);
    bool expire_at(const std::string& key, long long unix_seconds);
    long long ttl(const std::string& key) const;
    std::optional<long long> expiry_unix_seconds(const std::string& key) const;
    bool persist(const std::string& key);

    // Sweeps every shard for expired keys, one shard at a time (each
    // shard's own mutex, held only for that shard's sweep -- other
    // shards remain fully available to other threads throughout).
    size_t sweep_expired();

    // Sum of all shards' sizes. Takes each shard's lock briefly, one
    // at a time -- there's no global size counter to avoid a
    // cross-shard write on every set()/del() that would reintroduce
    // exactly the contention we're trying to remove.
    size_t size() const;

    size_t num_shards() const { return shards_.size(); }

    // Holds the lock for `key`'s shard for the duration of `fn`,
    // passing it the shard's underlying Store. This exists so a
    // caller (Server::dispatch) can do a store mutation AND its AOF
    // log append as one atomic unit relative to that key.
    //
    // Why this matters: the convenience methods above (set_string,
    // del, etc.) only hold the shard lock for the duration of that
    // single call -- it's released the instant the method returns.
    // If a caller did `store.set_string(k, v); aof.append(...)` as
    // two separate calls, two threads writing the SAME key
    // concurrently could have their AOF log entries land in a
    // different relative order than their actual store mutations
    // happened in. A crash-and-replay would then reconstruct a
    // different final value than what was really in memory -- a
    // real, if narrow, correctness bug. Wrapping both operations in
    // one with_shard_lock call closes that gap.
    template <typename Func>
    auto with_shard_lock(const std::string& key, Func&& fn) -> decltype(fn(std::declval<Store&>())) {
        Shard& shard = *shards_[shard_index(key)];
        std::lock_guard<std::mutex> lock(shard.mutex);
        return fn(shard.store);
    }

private:
    struct Shard {
        mutable std::mutex mutex;
        Store store;
    };

    // unique_ptr per shard: std::mutex is neither copyable nor
    // movable, so a plain std::vector<Shard> can't be resized/
    // constructed the way we need. A vector of unique_ptr<Shard>
    // sidesteps that entirely.
    std::vector<std::unique_ptr<Shard>> shards_;

    size_t shard_index(const std::string& key) const;
};