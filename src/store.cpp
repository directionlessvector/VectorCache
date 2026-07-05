#include "store.h"

Store::Store(size_t initial_buckets) : buckets_(initial_buckets), count_(0) {
    // TODO: anything else to initialize? (probably not, but think about it)
}

size_t Store::bucket_index(const std::string& key) const {
    return std::hash<std::string>{}(key) % buckets_.size();
}

bool Store::is_expired(const Entry& entry) {
    return entry.expires_at.has_value() && Clock::now() >= *entry.expires_at;
}

Store::Entry* Store::find_live(const std::string& key) {
    size_t idx = bucket_index(key);
    auto& bucket = buckets_[idx];

    for (auto it = bucket.begin(); it != bucket.end(); ++it) {
        if (it->key == key) {
            if (is_expired(*it)) {
                // Lazy expiry: the key is logically gone the moment
                // anyone tries to touch it, even if the background
                // sweep hasn't gotten to it yet.
                bucket.erase(it);
                --count_;
                return nullptr;
            }
            return &(*it);
        }
    }
    return nullptr;
}

const Store::Entry* Store::find_live(const std::string& key) const {
    // Delegate to the non-const version via const_cast — safe here
    // because find_live's only "mutation" is lazily deleting an
    // expired entry, which is a legitimate thing for a const-facing
    // lookup to do (it's not changing any LIVE data, and callers of
    // the const overload never see the entry that got removed
    // anyway). This mirrors how real Redis treats lazy expiry as an
    // implementation detail of "reading," not really a write.
    return const_cast<Store*>(this)->find_live(key);
}

void Store::set(const std::string& key, Value value) {
    size_t idx = bucket_index(key);
    auto& bucket = buckets_[idx];

    for (auto& entry : bucket) {
        if (entry.key == key) {
            entry.value = std::move(value);
            entry.expires_at = std::nullopt; // SET clears any existing TTL
            return;
        }
    }

    bucket.push_back(Entry{key, std::move(value), std::nullopt});
    ++count_;

    // Check load factor AFTER inserting, so the decision reflects
    // the table's actual current occupancy.
    maybe_resize();
}

std::optional<Value> Store::get_raw(const std::string& key) const {
    const Entry* entry = find_live(key);
    if (!entry) return std::nullopt;
    return entry->value;
}

bool Store::del(const std::string& key) {
    size_t idx = bucket_index(key);
    auto& bucket = buckets_[idx];

    for (auto it = bucket.begin(); it != bucket.end(); ++it) {
        if (it->key == key) {
            bool was_live = !is_expired(*it);
            bucket.erase(it);
            --count_;
            // Match real Redis: DEL on an already-expired key still
            // "succeeds" from the caller's perspective in the sense
            // that the key is gone either way, but it should report
            // 0 (not found) since the key was already logically
            // expired, not actually deleted by this call.
            return was_live;
        }
    }
    return false;
}

bool Store::exists(const std::string& key) const {
    return find_live(key) != nullptr;
}

bool Store::expire(const std::string& key, long long seconds) {
    auto target = Clock::now() + std::chrono::seconds(seconds);
    Entry* entry = find_live(key);
    if (!entry) return false;
    entry->expires_at = target;
    return true;
}

bool Store::expire_at(const std::string& key, long long unix_seconds) {
    Entry* entry = find_live(key);
    if (!entry) return false;
    entry->expires_at = Clock::time_point(std::chrono::seconds(unix_seconds));
    return true;
}

long long Store::ttl(const std::string& key) const {
    const Entry* entry = find_live(key);
    if (!entry) return kTtlNoKey;
    if (!entry->expires_at.has_value()) return kTtlNoExpiry;

    auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
        *entry->expires_at - Clock::now());
    return remaining.count() >= 0 ? remaining.count() : 0;
}

std::optional<long long> Store::expiry_unix_seconds(const std::string& key) const {
    const Entry* entry = find_live(key);
    if (!entry || !entry->expires_at.has_value()) return std::nullopt;

    auto secs = std::chrono::duration_cast<std::chrono::seconds>(
        entry->expires_at->time_since_epoch());
    return secs.count();
}

bool Store::persist(const std::string& key) {
    Entry* entry = find_live(key);
    if (!entry || !entry->expires_at.has_value()) return false;
    entry->expires_at = std::nullopt;
    return true;
}

size_t Store::sweep_expired() {
    size_t removed = 0;
    for (auto& bucket : buckets_) {
        for (auto it = bucket.begin(); it != bucket.end(); ) {
            if (is_expired(*it)) {
                it = bucket.erase(it);
                --count_;
                ++removed;
            } else {
                ++it;
            }
        }
    }
    return removed;
}

void Store::set_string(const std::string& key, const std::string& value) {
    set(key, Value(value));
}

std::optional<std::string> Store::get_string(const std::string& key) const {
    auto raw = get_raw(key);
    if (!raw) return std::nullopt;

    if (!std::holds_alternative<StringValue>(*raw)) {
        throw WrongTypeError();
    }
    return std::get<StringValue>(*raw);
}

void Store::maybe_resize() {
    double load_factor = static_cast<double>(count_) / static_cast<double>(buckets_.size());
    if (load_factor > kMaxLoadFactor) {
        resize(buckets_.size() * 2);
    }
}

void Store::resize(size_t new_bucket_count) {
    std::vector<std::vector<Entry>> new_buckets(new_bucket_count);

    for (auto& bucket : buckets_) {
        for (auto& entry : bucket) {
            size_t new_idx = std::hash<std::string>{}(entry.key) % new_bucket_count;
            new_buckets[new_idx].push_back(std::move(entry));
        }
    }

    buckets_ = std::move(new_buckets);
}