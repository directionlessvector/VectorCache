# VectorCache

> A Redis-compatible in-memory key-value database built from scratch in C++17.

VectorCache is a learning-focused systems project that recreates the core building blocks of Redis, including a custom hash table, TCP server, RESP2 protocol, multithreading, TTL expiration, and Append-Only File (AOF) persistence.

---

## Features

- Custom hash table implementation (no `std::unordered_map` for the database)
- Dynamic resizing with configurable load factor
- RESP2 protocol implementation
- Compatible with `redis-cli`
- Multithreaded TCP server (thread-per-connection)
- Thread-safe storage using mutex synchronization
- TTL & EXPIRE support
- Background expiration of keys
- Append-Only File (AOF) persistence
- Automatic recovery from AOF on restart
- Configurable fsync policies
- Support for multiple Redis-like data types
  - Strings
  - Lists *(internal support)*
  - Hashes *(internal support)*

---

## Tech Stack

- **Language:** C++17
- **Networking:** POSIX TCP Sockets
- **Concurrency:** std::thread, std::mutex
- **Protocol:** RESP2
- **Persistence:** Append-Only File (AOF)
- **Build:** CMake

---

## Project Structure

```text
VectorCache
├── CMakeLists.txt
├── src
│   ├── main.cpp
│   ├── server_main.cpp
│   ├── server.cpp
│   ├── server.h
│   ├── store.cpp
│   ├── store.h
│   ├── resp.cpp
│   ├── resp.h
│   ├── aof.cpp
│   └── aof.h
├── tests
│   ├── ttl_test.cpp
│   └── aof_test.cpp
└── build
```

---

# Architecture

```
                 redis-cli
                     │
               RESP2 Protocol
                     │
              TCP Socket Server
                     │
      Thread-per-Connection Model
                     │
          Command Dispatcher
                     │
             Thread-safe Store
                     │
        Custom Hash Table Engine
                     │
        TTL + AOF Persistence
```

---

# Building

Clone the repository

```bash
git clone https://github.com/directionlessvector/VectorCache.git
cd VectorCache
```

Build

```bash
mkdir build
cd build

cmake ..
make
```

---

# Running

## REPL

```bash
./kvstore
```

Example

```text
SET name Priyank
GET name
DEL name
SIZE
QUIT
```

---

## TCP Server

```bash
./kvserver
```

Or enable persistence

```bash
./kvserver --aof mystore.aof --fsync everysec
```

---

# Connecting with redis-cli

```bash
redis-cli -p 6380
```

Example

```text
127.0.0.1:6380> PING
PONG

127.0.0.1:6380> SET name Priyank
OK

127.0.0.1:6380> GET name
"Priyank"

127.0.0.1:6380> DEL name
(integer) 1
```

---

# Supported Commands

| Command | Status |
|----------|--------|
| PING | ✅ |
| SET | ✅ |
| GET | ✅ |
| DEL | ✅ |
| EXISTS | ✅ |
| EXPIRE | ✅ |
| TTL | ✅ |

---

# Persistence

VectorCache implements Redis-inspired Append-Only File persistence.

Every write command is appended to an AOF log.

Example:

```
*3
$3
SET
$4
name
$7
Priyank
```

During startup, the server replays every command to reconstruct the database state.

Supported fsync policies

- `always`
- `everysec`
- `no`

---

# TTL

Keys can automatically expire.

```text
SET temp hello
EXPIRE temp 10
TTL temp
```

Expired keys are removed automatically.

---

# Design Decisions

### Custom Hash Table

Instead of relying on `std::unordered_map`, VectorCache implements a separate-chaining hash table to understand collision handling, resizing, and memory management.

---

### RESP2

The server implements the Redis Serialization Protocol (RESP2), allowing standard Redis clients like `redis-cli` to communicate without modification.

---

### Thread-per-Connection

Each client connection is handled by an independent thread.

A global mutex currently protects the shared store to ensure correctness. Future versions will introduce sharded locking for improved scalability.

---

### AOF

Every write operation is persisted as a Redis command, making crash recovery deterministic and simple.

---

# Roadmap

- [x] Custom Hash Table
- [x] TCP Server
- [x] RESP2 Protocol
- [x] Thread-per-Connection
- [x] TTL
- [x] Append Only File (AOF)
- [ ] Lists (`LPUSH`, `LRANGE`)
- [ ] Hashes (`HSET`, `HGET`)
- [ ] Sharded Locking
- [ ] Benchmarking
- [ ] RDB Snapshots
- [ ] Replication
- [ ] Pub/Sub

---

# What I Learned

Building VectorCache helped me gain a deeper understanding of

- Hash table internals
- Memory management
- TCP/IP networking
- Redis Serialization Protocol (RESP)
- Concurrent programming
- Thread synchronization
- File system persistence
- Database recovery techniques
- Systems programming in modern C++

---

## Inspiration

This project is inspired by Redis and was built as a learning exercise to understand the internals of high-performance in-memory databases.

---

## License

MIT License