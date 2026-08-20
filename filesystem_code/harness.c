#include "ff.h"
#include <stdio.h>
#include <unistd.h>
#include <stddef.h>
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

        /* --- read path: walk root, read every regular file --- */
        if (f_opendir(&dir, "RAM:/") == FR_OK) {
            while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0]) {
                char path[64];
                snprintf(path, sizeof(path), "RAM:/%s", fno.fname);
                if (fno.fattrib & AM_DIR) {
                    /* recurse one level into subdirectories */
                    DIR subdir;
                    if (f_opendir(&subdir, path) == FR_OK) {
                        FILINFO subfno;
                        while (f_readdir(&subdir, &subfno) == FR_OK && subfno.fname[0]) {
                            if (!(subfno.fattrib & AM_DIR)) {
                                char subpath[96];
                                snprintf(subpath, sizeof(subpath), "%s/%s", path, subfno.fname);
                                if (f_open(&file, subpath, FA_READ) == FR_OK) {
                                    BYTE rbuf[512]; UINT br;
                                    f_read(&file, rbuf, sizeof(rbuf), &br);
                                    f_close(&file);
                                }
                            }
                        }
                        f_closedir(&subdir);
                    }
                } else {
                    if (f_open(&file, path, FA_READ) == FR_OK) {
                        BYTE rbuf[512]; UINT br;
                        f_read(&file, rbuf, sizeof(rbuf), &br);
                        f_close(&file);
                    }
                }
            }
            f_closedir(&dir);
        }

        /* --- write path: create/write/delete, mkdir --- */
        if (f_open(&file, "RAM:/fuzzw.bin", FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {
            static const char wdata[] = "AFLplusplus-write-probe";
            UINT bw;
            f_write(&file, wdata, sizeof(wdata), &bw);
            f_close(&file);
            f_unlink("RAM:/fuzzw.bin");
        }
        f_mkdir("RAM:/fuzzdir");

        f_mount(NULL, "RAM:", 0);
    }
    return 0;
}
