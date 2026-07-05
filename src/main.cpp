#include <iostream>
#include <sstream>
#include <string>

#include "store.h"

// Simple REPL to exercise the Store before any networking exists.
// Commands: SET key value | GET key | DEL key | EXISTS key | SIZE | QUIT
int main() {
    Store store;
    std::string line;

    std::cout << "kvstore REPL (Day 1) - type QUIT to exit\n";

    while (true) {
        std::cout << "> ";
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        for (auto& c : cmd) c = static_cast<char>(toupper(c));

        if (cmd == "QUIT" || cmd == "EXIT") {
            break;
        } else if (cmd == "SET") {
            std::string key, value;
            if (!(iss >> key >> value)) {
                std::cout << "ERR usage: SET key value\n";
                continue;
            }
            store.set_string(key, value);
            std::cout << "OK\n";
        } else if (cmd == "GET") {
            std::string key;
            if (!(iss >> key)) {
                std::cout << "ERR usage: GET key\n";
                continue;
            }
            try {
                auto val = store.get_string(key);
                if (val) {
                    std::cout << *val << "\n";
                } else {
                    std::cout << "(nil)\n";
                }
            } catch (const WrongTypeError& e) {
                std::cout << e.what() << "\n";
            }
        } else if (cmd == "DEL") {
            std::string key;
            if (!(iss >> key)) {
                std::cout << "ERR usage: DEL key\n";
                continue;
            }
            std::cout << (store.del(key) ? "1\n" : "0\n");
        } else if (cmd == "EXISTS") {
            std::string key;
            if (!(iss >> key)) {
                std::cout << "ERR usage: EXISTS key\n";
                continue;
            }
            std::cout << (store.exists(key) ? "1\n" : "0\n");
        } else if (cmd == "SIZE") {
            std::cout << store.size() << "\n";
        } else {
            std::cout << "ERR unknown command '" << cmd << "'\n";
        }
    }

    std::cout << "bye\n";
    return 0;
}