#pragma once

#include <cstdint>
#include <string>
#include <vector>

// XOR-based encryption for student IDs.
// Key must be a 32-char hex string (16 bytes), matching the server's app.toml.
// MAG_XORKEY is provided at compile time via CMake.
#ifndef MAG_XORKEY
#define MAG_XORKEY "deadbeefcafebabedeadbeefcafebabe"
#endif

#ifndef MAG_HTTP_PORT
#define MAG_HTTP_PORT 9876
#endif

#ifndef MAG_UDP_PORT
#define MAG_UDP_PORT 9875
#endif

namespace mag {

struct Student {
    std::string uuid;
    std::string forename;
    std::string surname;
    std::string passphrase;
    bool is_new{true};
};

struct Target {
    std::string uuid;
    std::string forename;
    std::string surname;
    std::string passphrase_hint;
};

struct Question {
    std::string text;
    std::string answer;
};

struct ServerInfo {
    std::string ip;
    int port{MAG_HTTP_PORT};
};

// Decode a hex string into raw bytes.
inline std::vector<uint8_t> hex_decode(const std::string& hex) {
    std::vector<uint8_t> out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        auto nibble = [](char c) -> uint8_t {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return 0;
        };
        out.push_back((nibble(hex[i]) << 4) | nibble(hex[i + 1]));
    }
    return out;
}

// Encode raw bytes as lowercase hex.
inline std::string hex_encode(const std::vector<uint8_t>& data) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(data.size() * 2);
    for (uint8_t b : data) {
        out += digits[b >> 4];
        out += digits[b & 0xf];
    }
    return out;
}

// XOR-encrypt a student ID (uint64) using the compile-time key.
// Returns hex-encoded ciphertext.
inline std::string encrypt_id(uint64_t id) {
    const std::string key_hex = MAG_XORKEY;
    std::vector<uint8_t> key = hex_decode(key_hex);
    if (key.empty()) return "";

    std::string plain = std::to_string(id);
    std::vector<uint8_t> cipher(plain.size());
    for (size_t i = 0; i < plain.size(); ++i)
        cipher[i] = static_cast<uint8_t>(plain[i]) ^ key[i % key.size()];
    return hex_encode(cipher);
}

} // namespace mag
