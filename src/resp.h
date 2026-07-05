#pragma once

#include <string>
#include <vector>
#include <optional>

// RESP2 (REdis Serialization Protocol) — the real Redis wire format.
// Reference: https://redis.io/docs/reference/protocol-spec/
//
// Clients (including real redis-cli) send commands as RESP arrays of
// bulk strings, e.g. SET foo bar arrives as:
//   *3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n
//
// We also accept "inline commands" (a plain line like "SET foo bar\r\n")
// since that makes manual testing via `nc`/`telnet` easy without
// hand-encoding RESP arrays.

namespace resp {

// Attempts to parse ONE full command out of the front of `buffer`.
// If a full command is present, fills `out_args` with the command
// and its arguments, ERASES the consumed bytes from `buffer`, and
// returns true. If the buffer doesn't yet contain a complete command
// (e.g. a partial recv()), returns false and leaves `buffer` untouched
// — the caller should recv() more bytes and try again.
//
// This is the piece that makes TCP's "byte stream, not message
// stream" nature safe to handle: a command can legitimately arrive
// split across multiple recv() calls, and this function is designed
// to be called repeatedly as more bytes trickle in.
bool try_parse_command(std::string& buffer, std::vector<std::string>& out_args);

// --- Reply formatters ---
// Each returns the exact bytes to write back to the client socket.

std::string make_simple_string(const std::string& s); // +OK\r\n
std::string make_error(const std::string& msg);         // -ERR msg\r\n
std::string make_integer(long long n);                  // :123\r\n
std::string make_bulk_string(const std::string& s);      // $N\r\n...\r\n
std::string make_nil_bulk_string();                      // $-1\r\n
std::string make_array(const std::vector<std::string>& already_encoded_elements);

// Encodes a plain command (raw args, NOT pre-encoded) as a RESP
// array of bulk strings — i.e. exactly the wire format a real client
// sends for a command. Used by the AOF to write commands to disk in
// a format that try_parse_command can read straight back.
std::string make_array_of_bulk_strings(const std::vector<std::string>& raw_args);

} // namespace resp