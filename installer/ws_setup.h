#pragma once
#include <stdint.h>
#define WS_SETUP_ROOT "ux0:data/wayward"
#define WS_SETUP_VERSION "versions/fc2c5b5150a40f78"
typedef struct {
    const char *path;
    uint32_t offset,packed,bytes,crc,method;
    uint8_t hash[32];
} ws_setup_file;
extern const ws_setup_file ws_setup_files[];
extern const unsigned ws_setup_count;
extern const uint32_t ws_setup_apk_size;
extern const uint64_t ws_setup_output_bytes;
extern const uint8_t ws_setup_apk_hash[32],ws_setup_identity[32];
/* Progress is byte-based, including while hashing/reusing completed files.
 * Nonzero requests cancellation. Core performs no UI/thread/graphics calls. */
typedef int (*ws_setup_progress)(const char *,uint64_t,uint64_t,void *);
typedef struct { char message[256],apk[512];unsigned completed,reused; } ws_setup_result;
int ws_setup_find(const char *,ws_setup_result *);
int ws_setup_run(const char *,const char *,ws_setup_progress,void *,ws_setup_result *);
int ws_setup_ready(const char *);
uint64_t ws_setup_space(const char *);
void ws_setup_on_vita(void);
