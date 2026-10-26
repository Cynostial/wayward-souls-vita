#ifndef WS_MUTF8_H
#define WS_MUTF8_H
#include <stddef.h>
#include <stdint.h>
enum { WS_TEXT_OK = 0, WS_TEXT_INVALID = -1, WS_TEXT_NOMEM = -2 };
/* JNI modified UTF-8: NUL is C0 80; supplementary characters are encoded as
 * UTF-16 surrogate code units. Allocated results belong to the caller. */
int ws_mutf8_decode(const char *input, uint16_t **output, size_t *units);
int ws_mutf8_encode(const uint16_t *input, size_t units, char **output, size_t *bytes);
#endif
