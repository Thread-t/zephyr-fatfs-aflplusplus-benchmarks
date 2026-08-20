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
        if (len < 8) continue;

        /* first 4 bytes = seek offset (attacker controlled) */
        uint32_t seek_off = ((uint32_t)buf[0]<<24)|((uint32_t)buf[1]<<16)|
                            ((uint32_t)buf[2]<<8)|(uint32_t)buf[3];

        fuzz_disk_load(clean_disk, 65536);
        FATFS fs;
        if (f_mount(&fs, "RAM:", 1) != FR_OK) continue;

        FIL file;
        if (f_open(&file, "RAM:/seektest.bin",
                   FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {
            UINT bw;
            f_write(&file, buf + 4, (UINT)(len - 4), &bw);
            f_lseek(&file, seek_off);  /* fuzz: huge/wrapped seek offset */
            BYTE rbuf[512]; UINT br;
            f_read(&file, rbuf, sizeof(rbuf), &br);
            f_close(&file);
        }
        f_mount(NULL, "RAM:", 0);
    }
    return 0;
}
