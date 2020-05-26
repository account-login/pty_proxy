#include "doctest/doctest/doctest.h"

// system
#include <string.h>
#include <stdlib.h>
#include <string>
// proj
#include "base64.h"


using namespace std;


TEST_CASE("base64.simple.1") {
    uint8_t input[] = "base64";
    uint8_t encoded[100] = {};
    b64_encode(input, sizeof(input), encoded);
    uint8_t expected[] = "YmFzZTY0AA==";
    CAPTURE((const char *)encoded);
    CHECK(0 == memcmp(expected, encoded, sizeof(expected) - 1));

    uint8_t decoded[100] = {};
    size_t insize = b64_encoded_size(sizeof(input));
    size_t outsize = sizeof(input);
    REQUIRE(0 == b64_decode(encoded, &insize, decoded, &outsize));
    CHECK(0 == memcmp(input, decoded, sizeof(input)));
}

TEST_CASE("base64.simple.2") {
    // uint8_t input[] = "\0";
    uint8_t input[] = "a";
    uint8_t encoded[100] = {};
    b64_encode(input, sizeof(input), encoded);
    // uint8_t expected[] = "AAA=";
    uint8_t expected[] = "YQA=";
    CAPTURE((const char *)encoded);
    CHECK(0 == memcmp(expected, encoded, sizeof(expected) - 1));

    uint8_t decoded[100] = {};
    size_t insize = b64_encoded_size(sizeof(input));
    size_t outsize = sizeof(input);
    REQUIRE(0 == b64_decode(encoded, &insize, decoded, &outsize));
    CHECK(0 == memcmp(input, decoded, sizeof(input)));
}

TEST_CASE("base64.decode.ignore.non.coding.char") {
    string coded = "YmFzZTY0AA==";
    for (size_t i = 0; i < coded.size() + 1; ++i) {
        string var = coded;
        if (i < coded.size()) {
            var.insert(var.begin() + i, ' ');
        } else {
            var.push_back(' ');
        }

        uint8_t decoded[100] = {};
        size_t insize = var.size();
        size_t outsize = sizeof(decoded);
        REQUIRE(0 == b64_decode((const uint8_t *)var.data(), &insize, decoded, &outsize));
        // CAPTURE(var);
        // CAPTURE((const char *)decoded);
        uint8_t expected[] = "base64";
        CHECK(0 == memcmp(expected, decoded, sizeof(expected) - 1));
    }
}

TEST_CASE("base64.encode.rand") {
    const size_t N = 10;
    for (size_t i = 0; i < N; ++i) {
        for (size_t size = 0; size < 50; ++size) {
            uint8_t input[size];
            for (size_t j = 0; j < size; ++j) {
                input[j] = rand();
            }

            size_t encoded_size = b64_encoded_size(size);
            uint8_t encoded[encoded_size + 1];
            uint8_t mark = rand();
            encoded[encoded_size] = mark;
            b64_encode(input, size, encoded);

            // check over run
            REQUIRE(encoded[encoded_size] == mark);

            uint8_t decoded[size + 1];
            decoded[size] = mark;
            size_t outsize = encoded_size;
            size_t insize = size;
            REQUIRE(0 == b64_decode(encoded, &outsize, decoded, &insize));

            // size
            CHECK(encoded_size == outsize);
            CHECK(insize == size);
            // decoded content
            CHECK(0 == memcmp(input, decoded, size));
            // over run
            CHECK(mark == decoded[size]);
        }
    }
}
