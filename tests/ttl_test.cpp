#include "store.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cassert>

int main() {
    Store s;

    // 1. TTL on a nonexistent key
    assert(s.ttl("nope") == kTtlNoKey);
    std::cout << "PASS: ttl on missing key = -2\n";

    // 2. TTL on a key with no expiry
    s.set_string("permanent", "value");
    assert(s.ttl("permanent") == kTtlNoExpiry);
    std::cout << "PASS: ttl on key with no expiry = -1\n";

    // 3. Set an expiry, check ttl is roughly right
    s.set_string("shortlived", "value");
    bool ok = s.expire("shortlived", 10);
    assert(ok);
    long long t = s.ttl("shortlived");
    assert(t == 9 || t == 10); // allow for timing slop
    std::cout << "PASS: ttl after expire(10) is ~10, got " << t << "\n";

    // 4. Expire in the past -> key should be immediately gone (lazy expiry)
    s.set_string("alreadydead", "value");
    s.expire("alreadydead", -5); // 5 seconds in the past
    assert(!s.exists("alreadydead"));
    assert(s.ttl("alreadydead") == kTtlNoKey); // gone, not just "no expiry"
    std::cout << "PASS: negative expire causes immediate lazy expiry\n";

    // 5. Real-time expiry: set 1 second TTL, wait 1.2s, confirm gone
    s.set_string("expiresoon", "value");
    s.expire("expiresoon", 1);
    assert(s.exists("expiresoon"));
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    assert(!s.exists("expiresoon"));
    std::cout << "PASS: real-time 1s TTL actually expires after sleep\n";

    // 6. persist() removes TTL
    s.set_string("willpersist", "value");
    s.expire("willpersist", 100);
    assert(s.ttl("willpersist") > 0);
    bool persisted = s.persist("willpersist");
    assert(persisted);
    assert(s.ttl("willpersist") == kTtlNoExpiry);
    std::cout << "PASS: persist() clears TTL\n";

    // 7. SET clears any existing TTL (real Redis semantics)
    s.set_string("resettable", "v1");
    s.expire("resettable", 100);
    assert(s.ttl("resettable") > 0);
    s.set_string("resettable", "v2"); // plain SET again
    assert(s.ttl("resettable") == kTtlNoExpiry);
    std::cout << "PASS: SET clears existing TTL\n";

    // 8. expire_at with an absolute past timestamp -> immediate expiry
    s.set_string("expireat_past", "value");
    s.expire_at("expireat_past", 1000000); // unix time in 1970, way in the past
    assert(!s.exists("expireat_past"));
    std::cout << "PASS: expire_at with past timestamp expires immediately\n";

    // 9. sweep_expired actually removes dead entries from the table
    Store s2;
    s2.set_string("a", "1");
    s2.set_string("b", "2");
    s2.expire("a", -1); // already expired
    // Don't touch "a" via get/exists first -- sweep should find it directly.
    size_t removed = s2.sweep_expired();
    assert(removed == 1);
    assert(s2.size() == 1);
    std::cout << "PASS: sweep_expired removes expired key without prior lazy access\n";

    std::cout << "\nALL TTL TESTS PASSED\n";
    return 0;
}