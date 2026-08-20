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

        uint8_t name_len = buf[0] % 16 + 1;
        if (len < 1 + name_len) continue;

        char fname[64];
        snprintf(fname, sizeof(fname), "RAM:/");
        memcpy(fname + 5, buf + 1, name_len);
        fname[5 + name_len] = '\0';

        const unsigned char *data = buf + 1 + name_len;
        size_t data_len = len - 1 - name_len;

        fuzz_disk_load(clean_disk, 65536);
        FATFS fs;
        if (f_mount(&fs, "RAM:", 1) != FR_OK) continue;

        /* write first so we have something to read */
        FIL file;
        if (f_open(&file, fname, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {
            UINT bw;
            f_write(&file, data, (UINT)data_len, &bw);
            f_close(&file);
        }
        /* now fuzz the read path */
        if (f_open(&file, fname, FA_READ) == FR_OK) {
            BYTE rbuf[512]; UINT br;
            f_read(&file, rbuf, sizeof(rbuf), &br);
            /* also test seek then read */
            f_lseek(&file, data_len / 2);
            f_read(&file, rbuf, sizeof(rbuf), &br);
            f_close(&file);
        }
        f_mount(NULL, "RAM:", 0);
    }
    return 0;
}
