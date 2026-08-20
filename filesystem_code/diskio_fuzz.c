#include "ff.h"
#include "diskio.h"
#include <string.h>
#include <stddef.h>
#include <stdint.h>

#define DISK_SECTORS 128
#define SECTOR_SIZE  512
#define DISK_SIZE (DISK_SECTORS * SECTOR_SIZE)
static BYTE disk[DISK_SIZE];

void fuzz_disk_load(const unsigned char *data, size_t len) {
    memset(disk, 0, DISK_SIZE);
    if (len > DISK_SIZE) len = DISK_SIZE;
    memcpy(disk, data, len);
}

DSTATUS disk_status(BYTE pdrv) { return 0; }
DSTATUS disk_initialize(BYTE pdrv) { return 0; }

/* widen everything to uint64_t before comparing, so overflow can't hide an
 * out-of-range sector/count the way 32-bit arithmetic did before */
static int in_bounds(LBA_t sector, UINT count) {
    uint64_t s = (uint64_t)sector;
    uint64_t c = (uint64_t)count;
    if (s >= DISK_SECTORS) return 0;
    if (c > (uint64_t)DISK_SECTORS - s) return 0;
    return 1;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    if (!in_bounds(sector, count)) return RES_PARERR;
    memcpy(buff, disk + (size_t)sector * SECTOR_SIZE, (size_t)count * SECTOR_SIZE);
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    if (!in_bounds(sector, count)) return RES_PARERR;
    memcpy(disk + (size_t)sector * SECTOR_SIZE, buff, (size_t)count * SECTOR_SIZE);
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    switch (cmd) {
        case GET_SECTOR_COUNT: *(LBA_t*)buff = DISK_SECTORS; return RES_OK;
        case GET_SECTOR_SIZE:  *(WORD*)buff  = SECTOR_SIZE; return RES_OK;
        case GET_BLOCK_SIZE:   *(DWORD*)buff = 1; return RES_OK;
        default: return RES_OK;
    }
}
