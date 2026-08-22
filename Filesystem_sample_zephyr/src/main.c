/*
 * CHERI Zephyr FatFS fuzz replay — persistent UART loop
 * Compile once. Feed inputs via stdin pipe. No recompile needed.
 *
 * Packet format:
 *   [op 1B][name_len 1B][data_len_hi 1B][data_len_lo 1B]
 *   [name name_len bytes][data data_len bytes]
 */

#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/device.h>
#include <string.h>
#include <stdint.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define AUTOMOUNT_NODE DT_NODELABEL(ffs1)
FS_FSTAB_DECLARE_ENTRY(AUTOMOUNT_NODE);

#define DISK_NAME    "RAM"
#define DISK_SECTORS 128
#define SECTOR_SIZE  512
#define MAX_DATA     4096
#define MAX_NAME     32

#define OP_WRITE     0x01
#define OP_READ      0x02
#define OP_MKDIR     0x03
#define OP_RENAME    0x04
#define OP_UNLINK    0x05
#define OP_STAT      0x06
#define OP_TRUNCATE  0x07
#define OP_MOUNT_RAW 0x08

static const struct device *uart_dev;
static uint8_t sector_buf[DISK_SECTORS * SECTOR_SIZE];
static uint8_t data_buf[MAX_DATA];
static char    name_buf[MAX_NAME + 8];
static char    name2_buf[MAX_NAME + 8];

/* block until one byte arrives on UART */
static uint8_t uart_getc(void)
{
	unsigned char c;

	while (uart_poll_in(uart_dev, &c) < 0) {
		k_sleep(K_MSEC(1));
	}
	return (uint8_t)c;
}

/* read exactly n bytes — blocks until all arrive */
static void uart_read_exact(uint8_t *dst, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		dst[i] = uart_getc();
	}
}

static void mount_clean(void)
{
	memset(sector_buf, 0, sizeof(sector_buf));
	disk_access_init(DISK_NAME);
	disk_access_write(DISK_NAME, sector_buf, 0, DISK_SECTORS);
	struct fs_mount_t *mp = &FS_FSTAB_ENTRY(AUTOMOUNT_NODE);
	fs_mount(mp);
}

static void unmount_all(void)
{
	struct fs_mount_t *mp = &FS_FSTAB_ENTRY(AUTOMOUNT_NODE);
	fs_unmount(mp);
}

int main(void)
{
	uart_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
	if (!device_is_ready(uart_dev)) {
		printk("UART not ready\n");
		return -1;
	}

	printk("FUZZ_READY\n");

	while (1) {
		/* read 4-byte header */
		uint8_t hdr[4];
		uart_read_exact(hdr, 4);

		uint8_t  op       = hdr[0];
		uint8_t  name_len = hdr[1] % MAX_NAME;
		uint16_t data_len = ((uint16_t)hdr[2] << 8) | hdr[3];
		if (data_len > MAX_DATA) data_len = MAX_DATA;

		/* read name */
		memset(name_buf, 0, sizeof(name_buf));
		snprintk(name_buf, sizeof(name_buf), "/RAM:/");
		if (name_len > 0) {
			uint8_t tmp[MAX_NAME];
			uart_read_exact(tmp, name_len);
			memcpy(name_buf + 6, tmp, name_len);
		}
		name_buf[6 + name_len] = '\0';

		/* read data */
		memset(data_buf, 0, sizeof(data_buf));
		if (data_len > 0) {
			uart_read_exact(data_buf, data_len);
		}

		printk("OP_START op=0x%02x name=%s dlen=%d\n",
		       op, name_buf, data_len);

		int rc = 0;
		struct fs_file_t file;
		struct fs_dirent stat_entry;

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
			fs_file_t_init(&file);
			if (fs_open(&file, name_buf,
				    FS_O_CREATE | FS_O_WRITE) == 0) {
				fs_write(&file, data_buf, data_len);
				fs_close(&file);
			}
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
			memset(name2_buf, 0, sizeof(name2_buf));
			snprintk(name2_buf, sizeof(name2_buf), "/RAM:/");
			{
				size_t copy_len = data_len < MAX_NAME
						  ? data_len : MAX_NAME;
				memcpy(name2_buf + 6, data_buf, copy_len);
				name2_buf[6 + copy_len] = '\0';
			}
			mount_clean();
			fs_file_t_init(&file);
			if (fs_open(&file, name_buf,
				    FS_O_CREATE | FS_O_WRITE) == 0) {
				fs_write(&file, "x", 1);
				fs_close(&file);
			}
			rc = fs_rename(name_buf, name2_buf);
			unmount_all();
			printk("OP_DONE op=rename rc=%d\n", rc);
			break;

		case OP_UNLINK:
			mount_clean();
			fs_file_t_init(&file);
			if (fs_open(&file, name_buf,
				    FS_O_CREATE | FS_O_WRITE) == 0) {
				fs_close(&file);
			}
			rc = fs_unlink(name_buf);
			unmount_all();
			printk("OP_DONE op=unlink rc=%d\n", rc);
			break;

		case OP_STAT:
			mount_clean();
			rc = fs_stat(name_buf, &stat_entry);
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
			memset(sector_buf, 0, sizeof(sector_buf));
			memcpy(sector_buf, data_buf,
			       data_len < sizeof(sector_buf)
			       ? data_len : sizeof(sector_buf));
			disk_access_init(DISK_NAME);
			disk_access_write(DISK_NAME, sector_buf,
					  0, DISK_SECTORS);
			{
				struct fs_mount_t *mp =
					&FS_FSTAB_ENTRY(AUTOMOUNT_NODE);
				rc = fs_mount(mp);
				if (rc == 0) {
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

		case 0xFF:
		printk("FUZZ_EXIT\n");
		return 0;

		default:
			printk("OP_DONE op=unknown rc=-1\n");
			break;
		}

		/* signal host script that this op is fully done */
		printk("FUZZ_ACK\n");
	}

	return 0;
}
