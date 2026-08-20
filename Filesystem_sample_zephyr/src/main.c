/*
 * Copyright (c) 2025 Endress+Hauser AG
 * Modified for CHERI fuzz-input replay
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ff.h>
#include <zephyr/device.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/disk_access.h>
#include <string.h>

#include "fuzz_input.h"   /* provides fuzz_data[] and fuzz_data_len */

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define AUTOMOUNT_NODE DT_NODELABEL(ffs1)
FS_FSTAB_DECLARE_ENTRY(AUTOMOUNT_NODE);

#define DISK_NAME "RAM"
#define DISK_SECTORS 128
#define SECTOR_SIZE 512

int main(void)
{
	int rc;
	static uint8_t sector_buf[DISK_SECTORS * SECTOR_SIZE];

	printk("REPLAY_START\n");

	memset(sector_buf, 0, sizeof(sector_buf));
	memcpy(sector_buf, fuzz_data,
	       fuzz_data_len < sizeof(sector_buf) ? fuzz_data_len : sizeof(sector_buf));

	rc = disk_access_init(DISK_NAME);
	if (rc != 0) {
		LOG_ERR("disk_access_init failed: %d", rc);
		printk("REPLAY_DONE rc=disk_init_fail\n");
		return rc;
	}

	rc = disk_access_write(DISK_NAME, sector_buf, 0, DISK_SECTORS);
	if (rc != 0) {
		LOG_ERR("disk_access_write failed: %d", rc);
		printk("REPLAY_DONE rc=disk_write_fail\n");
		return rc;
	}

	struct fs_mount_t *auto_mount_point = &FS_FSTAB_ENTRY(AUTOMOUNT_NODE);

	rc = fs_mount(auto_mount_point);
	if (rc != 0) {
		LOG_ERR("fs_mount failed: %d", rc);
		printk("REPLAY_DONE rc=mount_fail\n");
		return 0;
	}

	struct fs_dir_t dir;
	struct fs_dirent entry;

	fs_dir_t_init(&dir);
	rc = fs_opendir(&dir, "/RAM:");
	if (rc == 0) {
		while (fs_readdir(&dir, &entry) == 0 && entry.name[0] != '\0') {
			if (!(entry.type == FS_DIR_ENTRY_DIR)) {
				char path[80];
				snprintk(path, sizeof(path), "/RAM:/%s", entry.name);

				struct fs_file_t file;
				fs_file_t_init(&file);
				if (fs_open(&file, path, FS_O_READ) == 0) {
					uint8_t rbuf[512];
					fs_read(&file, rbuf, sizeof(rbuf));
					fs_close(&file);
				}
			}
		}
		fs_closedir(&dir);
	}

	fs_unmount(auto_mount_point);

	printk("REPLAY_DONE rc=ok\n");
	return 0;
}
