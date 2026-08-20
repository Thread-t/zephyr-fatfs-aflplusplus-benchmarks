cat > ~/CHERI-zephyr/samples/fatfs_shell/src/main.c << 'EOF'
/*
 * CHERI Zephyr FatFS fuzz replay — persistent UART loop
 * Compile once. Feed different inputs via stdin pipe. No recompile needed.
 *
 * Packet format:
 *   [op 1B][name_len 1B][name name_len bytes][data_len_hi 1B][data_len_lo 1B][data ...]
 */

#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/disk_access.h>
#include <string.h>
#include <stdint.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define AUTOMOUNT_NODE DT_NODELABEL(ffs1)
FS_FSTAB_DECLARE_ENTRY(AUTOMOUNT_NODE);

#define DISK_NAME    "RAM"
#define DISK_SECTORS 128
#define SECTOR_SIZE  512
#define MAX_DATA     4096
#define MAX_NAME     64

/* op codes — must match your host harnesses */
#define OP_WRITE     0x01
#define OP_READ      0x02
#define OP_MKDIR     0x03
#define OP_RENAME    0x04
#define OP_UNLINK    0x05
#define OP_STAT      0x06
#define OP_TRUNCATE  0x07
#define OP_MOUNT_RAW 0x08

static uint8_t sector_buf[DISK_SECTORS * SECTOR_SIZE];
static uint8_t data_buf[MAX_DATA];
static char    name_buf[MAX_NAME + 16];   /* +16 for "/RAM:/" prefix */

/* read exactly n bytes from console (UART stdin) */
static int uart_read_exact(uint8_t *dst, size_t n) {
    for (size_t i = 0; i < n; i++) {
        int c = getchar();
        if (c == EOF) return -1;
        dst[i] = (uint8_t)c;
    }
    return 0;
}

static void mount_clean(void) {
    /* zero disk then let FatFS create a fresh volume */
    memset(sector_buf, 0, sizeof(sector_buf));
    disk_access_init(DISK_NAME);
    disk_access_write(DISK_NAME, sector_buf, 0, DISK_SECTORS);
    struct fs_mount_t *mp = &FS_FSTAB_ENTRY(AUTOMOUNT_NODE);
    fs_mount(mp);
}

static void unmount_all(void) {
    struct fs_mount_t *mp = &FS_FSTAB_ENTRY(AUTOMOUNT_NODE);
    fs_unmount(mp);
}

int main(void) {
    printk("FUZZ_READY\n");

    while (1) {
        uint8_t hdr[4];
        if (uart_read_exact(hdr, 4) < 0) break;

        uint8_t  op       = hdr[0];
        uint8_t  name_len = hdr[1] % MAX_NAME;
        uint16_t data_len = ((uint16_t)hdr[2] << 8) | hdr[3];
        if (data_len > MAX_DATA) data_len = MAX_DATA;

        memset(name_buf, 0, sizeof(name_buf));
        memcpy(name_buf, "/RAM:/", 6);

        if (name_len > 0) {
            uint8_t tmp[MAX_NAME];
            if (uart_read_exact(tmp, name_len) < 0) break;
            memcpy(name_buf + 6, tmp, name_len);
        }
        name_buf[6 + name_len] = '\0';

        if (data_len > 0) {
            if (uart_read_exact(data_buf, data_len) < 0) break;
        }

        printk("OP_START op=0x%02x name=%s dlen=%d\n", op, name_buf, data_len);

        int rc = 0;
        struct fs_file_t file;
        struct fs_dirent stat;

        switch (op) {

        case OP_WRITE:
            mount_clean();
            fs_file_t_init(&file);
            rc = fs_open(&file, name_buf, FS_O_CREATE | FS_O_WRITE);
            if (rc == 0) {
                rc = fs_write(&file, data_buf, data_len);
                fs_close(&file);
            }
            unmount_all();
            printk("OP_DONE op=write rc=%d\n", rc);
            break;

        case OP_READ:
            mount_clean();
            /* first create a file with the fuzz data as content */
            fs_file_t_init(&file);
            if (fs_open(&file, name_buf, FS_O_CREATE | FS_O_WRITE) == 0) {
                fs_write(&file, data_buf, data_len);
                fs_close(&file);
            }
            /* now read it back */
            fs_file_t_init(&file);
            rc = fs_open(&file, name_buf, FS_O_READ);
            if (rc == 0) {
                uint8_t rbuf[512];
                rc = fs_read(&file, rbuf, sizeof(rbuf));
                fs_close(&file);
            }
            unmount_all();
            printk("OP_DONE op=read rc=%d\n", rc);
            break;

        case OP_MKDIR:
            mount_clean();
            rc = fs_mkdir(name_buf);
            unmount_all();
            printk("OP_DONE op=mkdir rc=%d\n", rc);
            break;

        case OP_RENAME:
        {
            /* data_buf = new name bytes */
            char newname[MAX_NAME + 16];
            snprintf(newname, sizeof(newname), "/RAM:/");
            size_t copy_len = data_len < MAX_NAME ? data_len : MAX_NAME;
            memcpy(newname + 6, data_buf, copy_len);
            newname[6 + copy_len] = '\0';
            mount_clean();
            /* create source */
            fs_file_t_init(&file);
            if (fs_open(&file, name_buf, FS_O_CREATE | FS_O_WRITE) == 0) {
                fs_write(&file, "x", 1, NULL);
                fs_close(&file);
            }
            rc = fs_rename(name_buf, newname);
            unmount_all();
            printk("OP_DONE op=rename rc=%d\n", rc);
            break;
        }

        case OP_UNLINK:
            mount_clean();
            fs_file_t_init(&file);
            if (fs_open(&file, name_buf, FS_O_CREATE | FS_O_WRITE) == 0) {
                fs_close(&file);
            }
            rc = fs_unlink(name_buf);
            unmount_all();
            printk("OP_DONE op=unlink rc=%d\n", rc);
            break;

        case OP_STAT:
            mount_clean();
            rc = fs_stat(name_buf, &stat);
            unmount_all();
            printk("OP_DONE op=stat rc=%d\n", rc);
            break;

        case OP_TRUNCATE:
            mount_clean();
            fs_file_t_init(&file);
            rc = fs_open(&file, name_buf, FS_O_CREATE | FS_O_RDWR);
            if (rc == 0) {
                fs_write(&file, data_buf, data_len);
                fs_seek(&file, 0, FS_SEEK_SET);
                rc = fs_truncate(&file, data_len / 2);
                fs_close(&file);
            }
            unmount_all();
            printk("OP_DONE op=truncate rc=%d\n", rc);
            break;

        case OP_MOUNT_RAW:
            /* data_buf = raw disk image bytes — parse corrupted disk */
            memset(sector_buf, 0, sizeof(sector_buf));
            memcpy(sector_buf, data_buf, data_len < sizeof(sector_buf) ? data_len : sizeof(sector_buf));
            disk_access_init(DISK_NAME);
            disk_access_write(DISK_NAME, sector_buf, 0, DISK_SECTORS);
            {
                struct fs_mount_t *mp = &FS_FSTAB_ENTRY(AUTOMOUNT_NODE);
                rc = fs_mount(mp);
                if (rc == 0) {
                    /* try reading root to stress parser */
                    struct fs_dir_t dir;
                    fs_dir_t_init(&dir);
                    if (fs_opendir(&dir, "/RAM:") == 0) {
                        struct fs_dirent entry;
                        fs_readdir(&dir, &entry);
                        fs_closedir(&dir);
                    }
                    fs_unmount(mp);
                }
            }
            printk("OP_DONE op=mount_raw rc=%d\n", rc);
            break;

        default:
            printk("OP_DONE op=unknown rc=-1\n");
            break;
        }
    }

    printk("FUZZ_EXIT\n");
    return 0;
}
EOF