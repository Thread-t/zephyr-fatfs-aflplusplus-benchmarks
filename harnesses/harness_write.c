#include "../src/ff.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

void fuzz_disk_load(const unsigned char*, size_t);

__AFL_FUZZ_INIT();

/* ensure a clean mountable disk is always present */
static void reset_disk(void) {
    extern unsigned char clean_disk[65536];
    fuzz_disk_load(clean_disk, 65536);
}

int main(void) {
#ifdef __AFL_HAVE_MANUAL_CONTROL
    __AFL_INIT();
#endif
    unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF;

    while (__AFL_LOOP(10000)) {
        int len = __AFL_FUZZ_TESTCASE_LEN;
        if (len < 4) continue;

        /* fuzz input = [name_len 1B][name N bytes][data...] */
        uint8_t name_len = buf[0] % 32 + 1;
        if (len < 1 + name_len) continue;

        char fname[64];
        memset(fname, 0, sizeof(fname));
        snprintf(fname, sizeof(fname), "RAM:/");
        memcpy(fname + 5, buf + 1, name_len);
        fname[5 + name_len] = '\0';

        const unsigned char *data    = buf + 1 + name_len;
        size_t              data_len = len - 1 - name_len;

        /* always mount a fresh clean disk so write bugs are about the
         * write path, not about parsing a corrupted boot sector */
        reset_disk();
        FATFS fs;
        if (f_mount(&fs, "RAM:", 1) != FR_OK) continue;

        FIL file;
        if (f_open(&file, fname, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {
            UINT bw;
            f_write(&file, data, (UINT)data_len, &bw);
            /* seek back and read — exercises both paths */
            f_lseek(&file, 0);
            uint8_t rbuf[512]; UINT br;
            f_read(&file, rbuf, sizeof(rbuf), &br);
            f_close(&file);
            f_unlink(fname);
        }
        f_mount(NULL, "RAM:", 0);
    }
    return 0;
}
