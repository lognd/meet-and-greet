#include <catch2/catch_all.hpp>
#include "data.h"

using namespace mag;

// ---------------------------------------------------------------------------
// hex_decode / hex_encode
// ---------------------------------------------------------------------------

TEST_CASE("hex_encode and hex_decode roundtrip", "[data][hex]") {
    SECTION("empty") {
        CHECK(hex_encode({}) == "");
        CHECK(hex_decode("").empty());
    }
    SECTION("single byte") {
        std::vector<uint8_t> in = {0xAB};
        CHECK(hex_encode(in) == "ab");
        CHECK(hex_decode("ab") == in);
    }
    SECTION("multi byte") {
        std::vector<uint8_t> in = {0xDE, 0xAD, 0xBE, 0xEF};
        CHECK(hex_encode(in) == "deadbeef");
        CHECK(hex_decode("deadbeef") == in);
    }
    SECTION("uppercase input to hex_decode") {
        std::vector<uint8_t> expected = {0xFF, 0x00};
        CHECK(hex_decode("FF00") == expected);
    }
    SECTION("roundtrip arbitrary bytes") {
        std::vector<uint8_t> in;
        for (int i = 0; i < 256; ++i) in.push_back(static_cast<uint8_t>(i));
        CHECK(hex_decode(hex_encode(in)) == in);
    }
}

// ---------------------------------------------------------------------------
// encrypt_id (uses compile-time MAG_XORKEY)
// ---------------------------------------------------------------------------

TEST_CASE("encrypt_id produces hex string", "[data][crypto]") {
    auto enc = encrypt_id(12345678);
    REQUIRE(!enc.empty());
    for (char c : enc)
        REQUIRE(std::string("0123456789abcdef").find(c) != std::string::npos);
}

TEST_CASE("encrypt_id output length equals 2 * digit count", "[data][crypto]") {
    // "12345678" is 8 chars -> 16 hex chars
    CHECK(encrypt_id(12345678).size() == 16);
    // "0" is 1 char -> 2 hex chars
    CHECK(encrypt_id(0).size() == 2);
    // "999" is 3 chars -> 6 hex chars
    CHECK(encrypt_id(999).size() == 6);
}

TEST_CASE("different IDs produce different ciphertexts", "[data][crypto]") {
    CHECK(encrypt_id(1) != encrypt_id(2));
    CHECK(encrypt_id(100) != encrypt_id(101));
}

TEST_CASE("encrypt_id is deterministic", "[data][crypto]") {
    CHECK(encrypt_id(42) == encrypt_id(42));
    CHECK(encrypt_id(999999) == encrypt_id(999999));
}

TEST_CASE("large student ID stays correct length", "[data][crypto]") {
    uint64_t big = 999'999'999'999ULL;
    auto enc = encrypt_id(big);
    // "999999999999" is 12 digits
    CHECK(enc.size() == 24);
}

// ---------------------------------------------------------------------------
// ServerInfo / Target / Student struct defaults
// ---------------------------------------------------------------------------

TEST_CASE("ServerInfo default port", "[data][structs]") {
    ServerInfo s;
    CHECK(s.port == MAG_HTTP_PORT);
}

TEST_CASE("Student is_new defaults true", "[data][structs]") {
    Student s;
    CHECK(s.is_new == true);
}
