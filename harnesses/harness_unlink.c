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
        if (len < 2) continue;

        char fname[64];
        snprintf(fname, sizeof(fname), "RAM:/");
        uint8_t name_len = buf[0] % 16 + 1;
        if (len < 1 + name_len) continue;
        memcpy(fname + 5, buf + 1, name_len);
        fname[5 + name_len] = '\0';

        fuzz_disk_load(clean_disk, 65536);
        FATFS fs;
        if (f_mount(&fs, "RAM:", 1) != FR_OK) continue;

        FIL file;
        if (f_open(&file, fname, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {
            UINT bw;
            f_write(&file, buf, (UINT)len, &bw);
            f_close(&file);
            f_unlink(fname);    /* fuzz: delete just-written file */
        }
        f_mount(NULL, "RAM:", 0);
    }
    return 0;
}
