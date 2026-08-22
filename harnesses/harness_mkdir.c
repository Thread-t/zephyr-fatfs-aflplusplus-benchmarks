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

        char dname[64];
        snprintf(dname, sizeof(dname), "RAM:/");
        uint8_t name_len = buf[0] % 16 + 1;
        if (len < 1 + name_len) continue;
        memcpy(dname + 5, buf + 1, name_len);
        dname[5 + name_len] = '\0';

        fuzz_disk_load(clean_disk, 65536);
        FATFS fs;
        if (f_mount(&fs, "RAM:", 1) != FR_OK) continue;

        f_mkdir(dname);

        /* try creating a file inside the new dir */
        char fpath[96];
        snprintf(fpath, sizeof(fpath), "%s/test.txt", dname);
        FIL file;
        if (f_open(&file, fpath, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {
            UINT bw;
            f_write(&file, buf, (UINT)len, &bw);
            f_close(&file);
        }
        f_mount(NULL, "RAM:", 0);
    }
    return 0;
}
