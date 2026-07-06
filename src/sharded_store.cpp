#include "sharded_store.h"

ShardedStore::ShardedStore(size_t num_shards) {
    if (num_shards == 0) num_shards = 1;
    shards_.reserve(num_shards);
    for (size_t i = 0; i < num_shards; ++i) {
        shards_.push_back(std::make_unique<Shard>());
    }
}

size_t ShardedStore::shard_index(const std::string& key) const {
    return std::hash<std::string>{}(key) % shards_.size();
}

void ShardedStore::set_string(const std::string& key, const std::string& value) {
    Shard& shard = *shards_[shard_index(key)];
    std::lock_guard<std::mutex> lock(shard.mutex);
    shard.store.set_string(key, value);
}

std::optional<std::string> ShardedStore::get_string(const std::string& key) const {
    auto& shard = *shards_[shard_index(key)];
    std::lock_guard<std::mutex> lock(shard.mutex);
    return shard.store.get_string(key);
}

bool ShardedStore::del(const std::string& key) {
    Shard& shard = *shards_[shard_index(key)];
    std::lock_guard<std::mutex> lock(shard.mutex);
    return shard.store.del(key);
}

bool ShardedStore::exists(const std::string& key) const {
    auto& shard = *shards_[shard_index(key)];
    std::lock_guard<std::mutex> lock(shard.mutex);
    return shard.store.exists(key);
}

bool ShardedStore::expire(const std::string& key, long long seconds) {
    Shard& shard = *shards_[shard_index(key)];
    std::lock_guard<std::mutex> lock(shard.mutex);
    return shard.store.expire(key, seconds);
}

bool ShardedStore::expire_at(const std::string& key, long long unix_seconds) {
    Shard& shard = *shards_[shard_index(key)];
    std::lock_guard<std::mutex> lock(shard.mutex);
    return shard.store.expire_at(key, unix_seconds);
}

long long ShardedStore::ttl(const std::string& key) const {
    auto& shard = *shards_[shard_index(key)];
    std::lock_guard<std::mutex> lock(shard.mutex);
    return shard.store.ttl(key);
}

std::optional<long long> ShardedStore::expiry_unix_seconds(const std::string& key) const {
    auto& shard = *shards_[shard_index(key)];
    std::lock_guard<std::mutex> lock(shard.mutex);
    return shard.store.expiry_unix_seconds(key);
}

bool ShardedStore::persist(const std::string& key) {
    Shard& shard = *shards_[shard_index(key)];
    std::lock_guard<std::mutex> lock(shard.mutex);
    return shard.store.persist(key);
}

size_t ShardedStore::sweep_expired() {
    size_t total = 0;
    for (auto& shard_ptr : shards_) {
        std::lock_guard<std::mutex> lock(shard_ptr->mutex);
        total += shard_ptr->store.sweep_expired();
    }
    return total;
}

size_t ShardedStore::size() const {
    size_t total = 0;
    for (auto& shard_ptr : shards_) {
        std::lock_guard<std::mutex> lock(shard_ptr->mutex);
        total += shard_ptr->store.size();
    }
    return total;
}