#include "server.h"
#include "resp.h"

#include <iostream>
#include <thread>
#include <cstring>
#include <cerrno>
#include <algorithm>
#include <chrono>

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

Server::Server(int port, const std::string& aof_path, FsyncPolicy policy)
    : port_(port), listen_fd_(-1), aof_(aof_path, policy) {
    load_aof(aof_path);
    sweep_thread_ = std::thread(&Server::run_periodic_sweep, this);
}

Server::~Server() {
    stop_sweep_ = true;
    if (sweep_thread_.joinable()) {
        sweep_thread_.join();
    }
    if (listen_fd_ >= 0) {
        close(listen_fd_);
    }
}

void Server::load_aof(const std::string& path) {
    size_t applied = 0;
    Aof::replay(path, [this, &applied](const std::vector<std::string>& args) {
        if (args.empty()) return;
        std::string cmd = args[0];
        std::transform(cmd.begin(), cmd.end(), cmd.begin(),
                        [](unsigned char c) { return std::toupper(c); });

        // Apply directly to store_ -- deliberately NOT going through
        // dispatch()/aof_.append(), since that would re-log every
        // replayed command right back into the file we're currently
        // replaying from.
        if (cmd == "SET" && args.size() == 3) {
            store_.set_string(args[1], args[2]);
        } else if (cmd == "DEL" && args.size() == 2) {
            store_.del(args[1]);
        } else if (cmd == "EXPIREAT" && args.size() == 3) {
            store_.expire_at(args[1], std::stoll(args[2]));
        } else if (cmd == "PERSIST" && args.size() == 2) {
            store_.persist(args[1]);
        }
        ++applied;
    });
    if (applied > 0) {
        std::cout << "Replayed " << applied << " commands from AOF, "
                  << store_.size() << " keys loaded" << std::endl;
    }
}

void Server::run_periodic_sweep() {
    while (!stop_sweep_) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::lock_guard<std::mutex> lock(store_mutex_);
        store_.sweep_expired();
    }
}

void Server::run() {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        throw std::runtime_error(std::string("socket() failed: ") + strerror(errno));
    }

    // Allow immediate re-bind after restart (avoids "Address already
    // in use" from lingering TIME_WAIT sockets during development).
    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port_));

    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        throw std::runtime_error(std::string("bind() failed: ") + strerror(errno));
    }

    if (listen(listen_fd_, /*backlog=*/64) < 0) {
        throw std::runtime_error(std::string("listen() failed: ") + strerror(errno));
    }

    std::cout << "kvserver listening on port " << port_ << "\n";

    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd < 0) {
            std::cerr << "accept() failed: " << strerror(errno) << "\n";
            continue;
        }

        // Thread-per-connection. Detached: the thread cleans up its
        // own socket and exits on disconnect; we don't join it.
        std::thread(&Server::handle_client, this, client_fd).detach();
    }
}

void Server::handle_client(int client_fd) {
    std::string buffer;
    char recv_buf[4096];

    while (true) {
        ssize_t n = recv(client_fd, recv_buf, sizeof(recv_buf), 0);
        if (n < 0) {
            std::cerr << "recv() error: " << strerror(errno) << "\n";
            break;
        }
        if (n == 0) {
            break; // client closed the connection
        }

        buffer.append(recv_buf, static_cast<size_t>(n));

        // A single recv() can contain zero, one, or several complete
        // commands (or a partial one) — drain everything that's
        // fully available before going back to recv().
        std::vector<std::string> args;
        while (resp::try_parse_command(buffer, args)) {
            std::string reply = dispatch(args);
            ssize_t sent = send(client_fd, reply.data(), reply.size(), 0);
            if (sent < 0) {
                std::cerr << "send() error: " << strerror(errno) << "\n";
                close(client_fd);
                return;
            }
        }
    }

    close(client_fd);
}

std::string Server::dispatch(const std::vector<std::string>& args) {
    if (args.empty()) {
        return resp::make_error("ERR empty command");
    }

    std::string cmd = args[0];
    std::transform(cmd.begin(), cmd.end(), cmd.begin(),
                    [](unsigned char c) { return std::toupper(c); });

    std::lock_guard<std::mutex> lock(store_mutex_);

    try {
        if (cmd == "PING") {
            return resp::make_simple_string("PONG");

        } else if (cmd == "SET") {
            if (args.size() != 3) return resp::make_error("ERR wrong number of arguments for 'SET'");
            store_.set_string(args[1], args[2]);
            aof_.append(args); // log verbatim: SET is already replay-safe as-is
            return resp::make_simple_string("OK");

        } else if (cmd == "GET") {
            if (args.size() != 2) return resp::make_error("ERR wrong number of arguments for 'GET'");
            auto val = store_.get_string(args[1]);
            return val ? resp::make_bulk_string(*val) : resp::make_nil_bulk_string();

        } else if (cmd == "DEL") {
            if (args.size() != 2) return resp::make_error("ERR wrong number of arguments for 'DEL'");
            bool removed = store_.del(args[1]);
            if (removed) aof_.append(args); // only log if it actually did something
            return resp::make_integer(removed ? 1 : 0);

        } else if (cmd == "EXISTS") {
            if (args.size() != 2) return resp::make_error("ERR wrong number of arguments for 'EXISTS'");
            return resp::make_integer(store_.exists(args[1]) ? 1 : 0);

        } else if (cmd == "EXPIRE") {
            if (args.size() != 3) return resp::make_error("ERR wrong number of arguments for 'EXPIRE'");
            long long seconds;
            try { seconds = std::stoll(args[2]); }
            catch (...) { return resp::make_error("ERR value is not an integer or out of range"); }

            bool ok = store_.expire(args[1], seconds);
            if (ok) {
                // Translate to an absolute timestamp before logging —
                // see aof.h's comment on why relative EXPIRE can't be
                // logged verbatim (replay days later would be wrong).
                auto abs_time = store_.expiry_unix_seconds(args[1]);
                if (abs_time) {
                    aof_.append({"EXPIREAT", args[1], std::to_string(*abs_time)});
                }
            }
            return resp::make_integer(ok ? 1 : 0);

        } else if (cmd == "TTL") {
            if (args.size() != 2) return resp::make_error("ERR wrong number of arguments for 'TTL'");
            return resp::make_integer(store_.ttl(args[1]));

        } else if (cmd == "PERSIST") {
            if (args.size() != 2) return resp::make_error("ERR wrong number of arguments for 'PERSIST'");
            bool ok = store_.persist(args[1]);
            if (ok) aof_.append(args);
            return resp::make_integer(ok ? 1 : 0);

        } else {
            return resp::make_error("ERR unknown command '" + args[0] + "'");
        }
    } catch (const WrongTypeError& e) {
        return resp::make_error(e.what());
    }
}