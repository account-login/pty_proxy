#pragma once

// system
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
#   define __EXTERN_C extern "C"
#else
#   define __EXTERN_C
#endif

__EXTERN_C inline size_t b64_encoded_size(size_t insize) {
    return (insize / 3 + !!(insize % 3)) * 4;
}

__EXTERN_C void b64_encode(const uint8_t *inbuf, size_t insize, uint8_t *outbuf);
__EXTERN_C int b64_decode(const uint8_t *inbuf, size_t *insize, uint8_t *outbuf, size_t *outsize);
