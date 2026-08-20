#include "../src/ff.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

void fuzz_disk_load(const unsigned char*, size_t);
extern unsigned char clean_disk[65536];

__AFL_FUZZ_INIT();

int main(void) {
#ifdef __AFL_HAVE_MANUAL_CONTROL
    __AFL_INIT();
#endif
    unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF;

    while (__AFL_LOOP(10000)) {
        int len = __AFL_FUZZ_TESTCASE_LEN;
        if (len < 6) continue;

        /* input = [src_len 1B][src name][dst_len 1B][dst name] */
        uint8_t src_len = buf[0] % 16 + 1;
        if (len < 1 + src_len + 1) continue;
        uint8_t dst_len = buf[1 + src_len] % 16 + 1;
        if (len < 1 + src_len + 1 + dst_len) continue;

        char src[32], dst[32];
        snprintf(src, sizeof(src), "RAM:/");
        snprintf(dst, sizeof(dst), "RAM:/");
        memcpy(src + 5, buf + 1, src_len);
        src[5 + src_len] = '\0';
        memcpy(dst + 5, buf + 1 + src_len + 1, dst_len);
        dst[5 + dst_len] = '\0';

        fuzz_disk_load(clean_disk, 65536);
        FATFS fs;
        if (f_mount(&fs, "RAM:", 1) != FR_OK) continue;

        /* create source file first */
        FIL file;
        if (f_open(&file, src, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {
            UINT bw;
            f_write(&file, "fuzz", 4, &bw);
            f_close(&file);
            f_rename(src, dst);    /* <-- target: rename */
        }
        f_mount(NULL, "RAM:", 0);
    }
    return 0;
}
