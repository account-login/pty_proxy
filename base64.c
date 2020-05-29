// system
#include <assert.h>
// self
#include "base64.h"


static const uint8_t k_encode_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static const uint8_t k_decode_table[256] = {
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 62, 255, 255, 255, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 255, 255, 255, 254, 255, 255, 255, 0, 1,
    2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
    24, 25, 255, 255, 255, 255, 255, 255, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35,
    36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255
};


void b64_encode(const uint8_t *inbuf, size_t insize, uint8_t *outbuf) {
    while (insize >= 3) {
        // outbuf[0] = k_encode_table[inbuf[0] & 0b00111111];
        outbuf[0] = k_encode_table[inbuf[0] >> 2];
        // outbuf[1] = k_encode_table[(inbuf[0] >> 6) | ((inbuf[1] & 0xf) << 2)];
        outbuf[1] = k_encode_table[0b00111111 & ((inbuf[0] << 4) | (inbuf[1] >> 4))];
        // outbuf[2] = k_encode_table[(inbuf[1] >> 4) | (inbuf[2] & 0b11) << 4];
        outbuf[2] = k_encode_table[0b00111111 & ((inbuf[1] << 2) | (inbuf[2] >> 6))];
        // outbuf[3] = k_encode_table[inbuf[2] >> 2];
        outbuf[3] = k_encode_table[0b00111111 & inbuf[2]];

        inbuf += 3;
        insize -= 3;
        outbuf += 4;
    }
    if (insize == 2) {
        // outbuf[0] = k_encode_table[inbuf[0] & 0b00111111];
        outbuf[0] = k_encode_table[inbuf[0] >> 2];
        // outbuf[1] = k_encode_table[(inbuf[0] >> 6) | ((inbuf[1] & 0xf) << 2)];
        outbuf[1] = k_encode_table[0b00111111 & ((inbuf[0] << 4) | (inbuf[1] >> 4))];
        // outbuf[2] = k_encode_table[inbuf[1] >> 4];
        outbuf[2] = k_encode_table[0b00111111 & (inbuf[1] << 2)];
        outbuf[3] = '=';
    } else if (insize == 1) {
        // outbuf[0] = k_encode_table[inbuf[0] & 0b00111111];
        outbuf[0] = k_encode_table[inbuf[0] >> 2];
        // outbuf[1] = k_encode_table[(inbuf[0] >> 6)];
        outbuf[1] = k_encode_table[0b00111111 & (inbuf[0] << 4)];
        outbuf[2] = '=';
        outbuf[3] = '=';
    }
}

int b64_decode(const uint8_t *inbuf, size_t *insize, uint8_t *outbuf, size_t *outsize) {
    const uint8_t *in_begin = inbuf;
    const uint8_t *out_begin = outbuf;
    const uint8_t *in_end = inbuf + *insize;
    const uint8_t *out_end = outbuf + *outsize;

    while (1) {
        const uint8_t *in_save = inbuf;

#define _READ(n) \
        uint8_t n = 0xff; \
        while (inbuf < in_end && n == 0xff) { \
            n = k_decode_table[inbuf[0]]; \
            inbuf++; \
        }

        _READ(a);
        _READ(b);
        _READ(c);
        _READ(d);

#undef _READ

        if (a == 0xff) {
            // inbuf end
            assert(inbuf == in_end);
            break;
        }
        if (b == 0xff || c == 0xff || d == 0xff) {
            // incomplete input
            inbuf = in_save;
            break;
        }
        if (a == 0xfe || b == 0xfe) {
            // unexpected padding
            return -1;
        }

        // uint8_t x = (a >> 0) | (b << 6);
        // uint8_t y = (b >> 2) | (c << 4);
        // uint8_t z = (c >> 4) | (d << 2);

        size_t block_len = 1 + (c != 0xfe) + (d != 0xfe);
        if (outbuf + block_len > out_end) {
            // outbuf end
            inbuf = in_save;
            break;
        }

        switch (block_len) {
        case 3:
            outbuf[2] = (c << 6) | (d >> 0);
            // fallthrough
        case 2:
            outbuf[1] = (b << 4) | (c >> 2);
            // fallthrough
        case 1:
            outbuf[0] = (a << 2) | (b >> 4);
        }
        outbuf += block_len;
    }

    // ok
    *insize = inbuf - in_begin;
    *outsize = outbuf - out_begin;
    return 0;
}
