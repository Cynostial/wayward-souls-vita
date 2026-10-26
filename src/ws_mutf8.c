#include "ws_mutf8.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

int ws_mutf8_decode(const char *input, uint16_t **output, size_t *units) {
    if (!input || !output || !units) return WS_TEXT_INVALID;
    *output = NULL; *units = 0;
    size_t bytes = strlen(input);
    if (bytes > INT_MAX || bytes > SIZE_MAX / sizeof(uint16_t) - 1) return WS_TEXT_INVALID;
    uint16_t *result = malloc((bytes + 1) * sizeof(*result));
    if (!result) return WS_TEXT_NOMEM;
    size_t i = 0, count = 0;
    while (i < bytes) {
        unsigned a = (unsigned char)input[i++], value;
        if (a < 0x80) value = a;
        else if (a >= 0xc0 && a < 0xe0) {
            if (i == bytes) goto invalid;
            unsigned b = (unsigned char)input[i++];
            if ((b & 0xc0) != 0x80) goto invalid;
            value = ((a & 31) << 6) | (b & 63);
            if (value && value < 0x80) goto invalid;
        } else if (a >= 0xe0 && a < 0xf0) {
            if (bytes - i < 2) goto invalid;
            unsigned b = (unsigned char)input[i++], c = (unsigned char)input[i++];
            if ((b & 0xc0) != 0x80 || (c & 0xc0) != 0x80) goto invalid;
            value = ((a & 15) << 12) | ((b & 63) << 6) | (c & 63);
            if (value < 0x800) goto invalid;
        } else goto invalid;
        result[count++] = (uint16_t)value;
    }
    result[count] = 0;
    *output = result; *units = count;
    return WS_TEXT_OK;
invalid:
    free(result); return WS_TEXT_INVALID;
}
int ws_mutf8_encode(const uint16_t *input, size_t units, char **output, size_t *bytes) {
    if ((!input && units) || !output || !bytes || units > (INT_MAX - 1u) / 3u)
        return WS_TEXT_INVALID;
    *output = NULL; *bytes = 0;
    char *result = malloc(units * 3 + 1);
    if (!result) return WS_TEXT_NOMEM;
    size_t count = 0;
    for (size_t i = 0; i < units; ++i) {
        unsigned c = input[i];
        if (c && c < 0x80) result[count++] = (char)c;
        else if (c < 0x800) {
            result[count++] = (char)(0xc0 | (c >> 6));
            result[count++] = (char)(0x80 | (c & 63));
        } else {
            result[count++] = (char)(0xe0 | (c >> 12));
            result[count++] = (char)(0x80 | ((c >> 6) & 63));
            result[count++] = (char)(0x80 | (c & 63));
        }
    }
    result[count] = 0;
    *output = result; *bytes = count;
    return WS_TEXT_OK;
}
