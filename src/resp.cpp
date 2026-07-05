#include "resp.h"

#include <charconv>

namespace resp {

namespace {

// Finds the index just past the next "\r\n" starting at `pos`.
// Returns std::string::npos if no terminator is present yet
// (meaning: not enough data has arrived, try again later).
size_t find_after_crlf(const std::string& buffer, size_t pos) {
    size_t crlf = buffer.find("\r\n", pos);
    if (crlf == std::string::npos) return std::string::npos;
    return crlf + 2;
}

// Parses a decimal integer from buffer[pos, end). Returns false on
// malformed input.
bool parse_int(const std::string& s, long long& out) {
    auto result = std::from_chars(s.data(), s.data() + s.size(), out);
    return result.ec == std::errc{} && result.ptr == s.data() + s.size();
}

// Attempts to parse a RESP array-of-bulk-strings command starting at
// buffer[0] == '*'. Returns true and sets consumed_len on success.
bool try_parse_multibulk(const std::string& buffer, std::vector<std::string>& out_args,
                          size_t& consumed_len) {
    size_t pos = 0;

    size_t line_end = find_after_crlf(buffer, pos);
    if (line_end == std::string::npos) return false; // incomplete

    // buffer[0] == '*', header is buffer[1 .. line_end-2)
    std::string count_str = buffer.substr(1, line_end - pos - 3);
    long long count = 0;
    if (!parse_int(count_str, count) || count < 0) {
        // Malformed — treat as "not parseable", caller can decide to
        // drop the connection. For simplicity we just say incomplete
        // (a real implementation would raise a protocol error here).
        return false;
    }

    pos = line_end;
    std::vector<std::string> args;
    args.reserve(static_cast<size_t>(count));

    for (long long i = 0; i < count; ++i) {
        if (pos >= buffer.size() || buffer[pos] != '$') return false; // incomplete
        size_t bulk_header_end = find_after_crlf(buffer, pos);
        if (bulk_header_end == std::string::npos) return false; // incomplete

        std::string len_str = buffer.substr(pos + 1, bulk_header_end - pos - 3);
        long long len = 0;
        if (!parse_int(len_str, len) || len < 0) return false;

        size_t data_start = bulk_header_end;
        size_t data_end = data_start + static_cast<size_t>(len);
        size_t after_data = data_end + 2; // +2 for trailing \r\n

        if (after_data > buffer.size()) return false; // incomplete

        args.push_back(buffer.substr(data_start, static_cast<size_t>(len)));
        pos = after_data;
    }

    out_args = std::move(args);
    consumed_len = pos;
    return true;
}

// Attempts to parse a plain inline command, e.g. "SET foo bar\r\n" or
// "SET foo bar\n". Splits on whitespace. Returns true and sets
// consumed_len if a full line has arrived.
bool try_parse_inline(const std::string& buffer, std::vector<std::string>& out_args,
                       size_t& consumed_len) {
    size_t newline = buffer.find('\n');
    if (newline == std::string::npos) return false; // incomplete

    size_t line_len = newline; // excludes the \n itself
    // Trim a trailing \r if present (so both \r\n and \n line endings work).
    if (line_len > 0 && buffer[line_len - 1] == '\r') --line_len;

    std::string line = buffer.substr(0, line_len);

    std::vector<std::string> args;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && isspace(static_cast<unsigned char>(line[i]))) ++i;
        size_t start = i;
        while (i < line.size() && !isspace(static_cast<unsigned char>(line[i]))) ++i;
        if (i > start) args.push_back(line.substr(start, i - start));
    }

    out_args = std::move(args);
    consumed_len = newline + 1;
    return true;
}

} // namespace

bool try_parse_command(std::string& buffer, std::vector<std::string>& out_args) {
    if (buffer.empty()) return false;

    size_t consumed_len = 0;
    bool ok;

    if (buffer[0] == '*') {
        ok = try_parse_multibulk(buffer, out_args, consumed_len);
    } else {
        ok = try_parse_inline(buffer, out_args, consumed_len);
    }

    if (!ok) return false;

    buffer.erase(0, consumed_len);

    // An empty inline line (just "\r\n") parses to zero args — treat
    // as "no command yet", let the caller loop try again on the
    // remaining buffer rather than dispatching an empty command.
    if (out_args.empty()) return try_parse_command(buffer, out_args);

    return true;
}

std::string make_simple_string(const std::string& s) {
    return "+" + s + "\r\n";
}

std::string make_error(const std::string& msg) {
    return "-" + msg + "\r\n";
}

std::string make_integer(long long n) {
    return ":" + std::to_string(n) + "\r\n";
}

std::string make_bulk_string(const std::string& s) {
    return "$" + std::to_string(s.size()) + "\r\n" + s + "\r\n";
}

std::string make_nil_bulk_string() {
    return "$-1\r\n";
}

std::string make_array(const std::vector<std::string>& already_encoded_elements) {
    std::string out = "*" + std::to_string(already_encoded_elements.size()) + "\r\n";
    for (const auto& elem : already_encoded_elements) {
        out += elem;
    }
    return out;
}

std::string make_array_of_bulk_strings(const std::vector<std::string>& raw_args) {
    std::string out = "*" + std::to_string(raw_args.size()) + "\r\n";
    for (const auto& arg : raw_args) {
        out += make_bulk_string(arg);
    }
    return out;
}

} // namespace resp