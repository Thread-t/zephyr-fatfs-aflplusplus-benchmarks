#include "../src/ff.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

void fuzz_disk_load(const unsigned char*, size_t);

__AFL_FUZZ_INIT();

int main(void) {
#ifdef __AFL_HAVE_MANUAL_CONTROL
    __AFL_INIT();
#endif
    unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF;
    while (__AFL_LOOP(10000)) {
        int len = __AFL_FUZZ_TESTCASE_LEN;
        /* mount raw corrupted image — then stat whatever dir entries it claims */
        fuzz_disk_load(buf, (size_t)len);
        FATFS fs;
        if (f_mount(&fs, "RAM:", 1) != FR_OK) continue;

        FILINFO fno;
        f_stat("RAM:/", &fno);        /* root */

        DIR dir;
        if (f_opendir(&dir, "RAM:/") == FR_OK) {
            while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0]) {
                char path[64];
                snprintf(path, sizeof(path), "RAM:/%s", fno.fname);
                FILINFO info;
                f_stat(path, &info);  /* stat each found entry */
            }
            f_closedir(&dir);
        }
        f_mount(NULL, "RAM:", 0);
    }
    return 0;
}
