#include "../src/ff.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>

void fuzz_disk_load(const unsigned char*, size_t);

__AFL_FUZZ_INIT();

int main(void) {
#ifdef __AFL_HAVE_MANUAL_CONTROL
    __AFL_INIT();
#endif
    unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF;

    while (__AFL_LOOP(10000)) {
        int len = __AFL_FUZZ_TESTCASE_LEN;
        fuzz_disk_load(buf, (size_t)len);

        FATFS fs;
        FIL file;
        FILINFO fno;
        DIR dir;

        if (f_mount(&fs, "RAM:", 1) != FR_OK) continue;

        if (f_opendir(&dir, "RAM:/") == FR_OK) {
            while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0]) {
                char path[64];
                snprintf(path, sizeof(path), "RAM:/%s", fno.fname);
                if (!(fno.fattrib & AM_DIR)) {
                    if (f_open(&file, path, FA_READ) == FR_OK) {
                        BYTE rbuf[512]; UINT br;
                        f_read(&file, rbuf, sizeof(rbuf), &br);
                        f_close(&file);
                    }
                }
            }
            f_closedir(&dir);
        }
        f_mount(NULL, "RAM:", 0);
    }
    return 0;
}
