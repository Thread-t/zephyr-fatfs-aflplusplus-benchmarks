/*
 * Copyright (c) 2025 Endress+Hauser AG
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ff.h>
#include <zephyr/device.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <string.h>
#include <stdlib.h>


LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define AUTOMOUNT_NODE DT_NODELABEL(ffs1)
FS_FSTAB_DECLARE_ENTRY(AUTOMOUNT_NODE);

#define BUFFER_SIZE 2048

#define UART_NODE DT_CHOSEN(zephyr_console)
static const struct device *const uart_dev = DEVICE_DT_GET(UART_NODE);

static char rx_buf[BUFFER_SIZE + 1];
static size_t rx_idx = 0;

/* Global state for a single active file instance */
static struct fs_file_t my_file;
/* Global state for a single active directory instance */
static struct fs_dir_t my_dir;
/* Global variable for storing file status */
static struct fs_dirent my_dirent;
/* Global variable for storing mount status */
static struct fs_statvfs my_statvfs;
/* Direct mount point to automounted node */
struct fs_mount_t *auto_mount_point = &FS_FSTAB_ENTRY(AUTOMOUNT_NODE);


/* Reusable operational storage for custom write payload extractions */
static char *file_data_payload;

#define FILE_PATH "/RAM:/"
#define FILE_PATH_LEN 6

static char full_path[BUFFER_SIZE + 10];
static char to_path[BUFFER_SIZE + 10];

// Variable declarations for file operations
static int open_rc, close_rc, write_rc, read_rc, seek_rc, tell_rc;
static int mkdir_rc, opendir_rc, readdir_rc, closedir_rc, unlink_rc;
static int rename_rc, truncate_rc, sync_rc, mount_rc, unmount_rc;
static int readmount_rc, stat_rc, statvfs_rc;
static char *alternate_ptr;
static unsigned int mode_flags;
static size_t write_bytes, read_bytes;
static off_t seek_offset, truncate_len;
static int seek_position, readmount_index;

/* Prepend FILE_PATH to build full path */
static void prepend_mount_point(char *full_path, const char *input_path) 
{
    /* 1. Defensive verification: Ensure input_path isn't NULL */
    if (!full_path) {
        return;
    }

    /* 2. Calculate input length */
    size_t input_len = strlen(input_path);

    /* 4. Copy the prefix macro string directly into the start of the buffer */
    memcpy(full_path, FILE_PATH, FILE_PATH_LEN);
    
    if (!input_path || (input_len == 0)) return;

    /* 5. Copy the input string exactly where the prefix ends */
    memcpy(&full_path[FILE_PATH_LEN], input_path, input_len);

    /* 6. Append your absolute null terminator boundary */
    full_path[FILE_PATH_LEN + input_len] = '\0';
}

/* Executes isolated system tasks based on parsed token choices */
static void process_file_operation(char *command_line)
{
    char *token_id;
    char *token_choice; // Choose parameters for the file operation
    char *token_param1; // Maps to: f_path
    char *token_param2; // Maps to: flag_byte or write_payload
    char *state_ptr;
    char *choice_ptr;

    /* Extract operational mode ID token safely */
    token_id = strtok_r(command_line, ",", &state_ptr);
    if (!token_id) {
        printk("Error: File operation mode absent.\n");
        return;
    }

    int operation_choice = atoi(token_id);
    
    /* Extract parameter choice token safely */
    token_choice = strtok_r(NULL, ",", &state_ptr);
    if (!token_choice) {
        printk("Error: Parameter choice absent.\n");
        return;
    }
    
    // strtok_r treats consecutive delimiters as a single delimiter
    int parameter = atoi(token_choice);
    // Include alternative pointer in token choice (in case needed)
    char *p_str = strtok_r(token_choice, "_", &choice_ptr);

    switch (operation_choice) {
        
        case 0: /* --- OPEN OPERATION --- */
            /* Expected structure: 0,<parameter>,<f_path>,<flag_byte> */
            token_param1 = strtok_r(NULL, ",", &state_ptr);
            token_param2 = strtok_r(NULL, ",", &state_ptr);
            
            
            if ( !token_param2 ) {
                printk("Error: open mode absent.\n");
                break;
            }
            
            // If LSB of parameter is 0, tokens are properly processed
            if ( !(parameter & 1) ) {
            	mode_flags = (unsigned int)strtoul(token_param2, NULL, 16);
            	prepend_mount_point(full_path, token_param1);
            	// printk("FILE path set at: %s.\n",full_path);
       	    }

	        // If 2nd LSB of parameter is 0, a working file pointer is passed
            if ( !(parameter & 2) ) {
            	fs_file_t_init(&my_file);
            	if ( !(parameter & 1) ) {
            	   open_rc = fs_open(&my_file, full_path, mode_flags);
            	} else {
            	   open_rc = fs_open(&my_file, token_param1, *token_param2);
            	}
            } else {
            	alternate_ptr = strtok_r(NULL, "_", &choice_ptr);
            	if ( !(parameter & 1) ) { 
            	   open_rc = fs_open(alternate_ptr, full_path, mode_flags);	// CHERI exception observed for character string
            	} else {
            	   open_rc = fs_open(alternate_ptr, token_param1, *token_param2);
            	}
            }
            
            
            if (open_rc == 0) {
                printk("SUCCESS: File opened.\n");
            } else {
                printk("FAILURE: File not opened, Error code: %d.\n", open_rc);
            }
            
            break;
            
      
        case 1: /* --- CLOSE OPERATION --- */
            /* Expected structure: 1,<parameter> */
            
            // If 2nd LSB of parameter is 0, a working file pointer is passed
            if ( !(parameter & 2) ) {
                close_rc = fs_close(&my_file);
            } else {
                alternate_ptr = strtok_r(NULL, "_", &choice_ptr);
                close_rc = fs_close(alternate_ptr);
            }
            
            if (close_rc == 0) {
                printk("SUCCESS: File closed.\n");
            } else {
                printk("FAILURE: File not closed, Error code: %d.\n", close_rc);
            }
            
            break;
            
        case 2: /* --- WRITE OPERATION --- */
            /* Expected structure: 2,<parameter>,<write_payload>,<bytes_count> */
            file_data_payload = strtok_r(NULL, ",", &state_ptr);
            token_param2 = strtok_r(NULL, ",", &state_ptr);
            
            
            if (!token_param2) {
                printk("Error: write bytes count absent.\n");
                break;
            }

            // If LSB of parameter is 0, tokens are properly processed
            if ( !(parameter & 1) ) {
            	write_bytes = (size_t)strtoul(token_param2, NULL, 10);
       	    }
            
            // If 2nd LSB of parameter is 0, a working file pointer is passed
            if ( !(parameter & 2) ) {
                if ( !(parameter & 1) ) {
                    write_rc = fs_write(&my_file, file_data_payload, write_bytes);
                } else {
                    write_rc = fs_write(&my_file, file_data_payload, *token_param2);
                }
            } else {
                alternate_ptr = strtok_r(NULL, "_", &choice_ptr);
                if ( !(parameter & 1) ) {
                    write_rc = fs_write(alternate_ptr, file_data_payload, write_bytes);
                } else {
                    write_rc = fs_write(alternate_ptr, file_data_payload, *token_param2);
                }
            }

            if ( (write_rc >= 0) && ((write_rc == write_bytes) || (write_rc == *token_param2)) ) {
                printk("SUCCESS: File write.\n");
            } else {
                printk("FAILURE: File write failed, Error code: %d.\n", write_rc);
            }

            break;


	    case 3: /* --- READ OPERATION --- */
            /* Expected structure: 3,<parameter>,<read_pointer>,<bytes_count> */
            token_param1 = strtok_r(NULL, ",", &state_ptr);
            
            
            if (!token_param1) {
                printk("Error: read bytes count absent.\n");
                break;
            }

            // If LSB of parameter is 0, tokens are properly processed
            if ( !(parameter & 1) ) {
            	read_bytes = (size_t)strtoul(token_param1, NULL, 10);
       	    }
            
            // If 2nd LSB of parameter is 0, a working file pointer is passed
            if ( !(parameter & 2) ) {
                if ( !(parameter & 1) ) {
                    read_rc = fs_read(&my_file, file_data_payload, read_bytes);
                } else {
                    read_rc = fs_read(&my_file, file_data_payload, *token_param1);
                }
            } else {
                alternate_ptr = strtok_r(NULL, "_", &choice_ptr);
                if ( !(parameter & 1) ) {
                    read_rc = fs_read(alternate_ptr, file_data_payload, read_bytes);
                } else {
                    read_rc = fs_read(alternate_ptr, file_data_payload, *token_param1);
                }
            }

            if (read_rc >= 0) {
                printk("SUCCESS: File read bytes %zd.\n", read_rc);
            } else {
                printk("FAILURE: File read failed, Error code: %d.\n", read_rc);
            }

            break;


        case 4: /* --- SEEK OPERATION --- */
            /* Expected structure: 4,<parameter>,<offset>,<initial-position> */
            token_param1 = strtok_r(NULL, ",", &state_ptr);
            token_param2 = strtok_r(NULL, ",", &state_ptr);
            
            
            if ((!token_param1) || (!token_param2)) {
                printk("Error: seek offset or initial position absent.\n");
                break;
            }

            // If LSB of parameter is 0, tokens are properly processed
            if ( !(parameter & 1) ) {
            	seek_offset = (off_t)strtoul(token_param1, NULL, 10);
                seek_position = (int)strtoul(token_param2, NULL, 10);
       	    }
            
            // If 2nd LSB of parameter is 0, a working file pointer is passed
            if ( !(parameter & 2) ) {
                if ( !(parameter & 1) ) {
                    seek_rc = fs_seek(&my_file, seek_offset, seek_position);
                } else {
                    seek_rc = fs_seek(&my_file, *token_param1, *token_param2);
                }
            } else {
                alternate_ptr = strtok_r(NULL, "_", &choice_ptr);
                if ( !(parameter & 1) ) {
                    seek_rc = fs_seek(alternate_ptr, seek_offset, seek_position);
                } else {
                    seek_rc = fs_seek(alternate_ptr, *token_param1, *token_param2);
                }
            }

            if (seek_rc == 0) {
                printk("SUCCESS: File seek.\n");
            } else {
                printk("FAILURE: File seek failed, Error code: %d.\n", seek_rc);
            }

            break;


        case 5: /* --- TELL OPERATION --- */
            /* Expected structure: 5,<parameter> */
            
            // If 2nd LSB of parameter is 0, a working file pointer is passed
            if ( !(parameter & 2) ) {
                tell_rc = fs_tell(&my_file);
            } else {
                alternate_ptr = strtok_r(NULL, "_", &choice_ptr);
                tell_rc = fs_tell(alternate_ptr);
            }
            
            if (tell_rc >= 0) {
                printk("SUCCESS: File tell position %d.\n", tell_rc);
            } else {
                printk("FAILURE: File tell, Error code: %d.\n", tell_rc);
            }
            
            break;


        case 6: /* --- MKDIR OPERATION --- */
            /* Expected structure: 6,<parameter>,<dir_path> */
            token_param1 = strtok_r(NULL, ",", &state_ptr);
            
            
            if (!token_param1) {
                printk("Error: mkdir path absent.\n");
                break;
            }

            // If LSB of parameter is 0, tokens are properly processed
            if ( !(parameter & 1) ) {
            	prepend_mount_point(full_path, token_param1);
                mkdir_rc = fs_mkdir(full_path);
       	    } else {
                mkdir_rc = fs_mkdir(token_param1);
            }

            if (mkdir_rc == 0) {
                printk("SUCCESS: mkdir.\n");
            } else {
                printk("FAILURE: mkdir, Error code: %d.\n", mkdir_rc);
            }

            break;


        case 7: /* --- OPENDIR OPERATION --- */
            /* Expected structure: 7,<parameter>,<dir_path> */
            token_param1 = strtok_r(NULL, ",", &state_ptr);
            
            
            if ( !token_param1 ) {
                printk("Error: opendir path absent.\n");
                break;
            }
            
            // If LSB of parameter is 0, tokens are properly processed
            if ( !(parameter & 1) ) {
            	prepend_mount_point(full_path, token_param1);
       	    }

	        // If 2nd LSB of parameter is 0, a working directory pointer is passed
            if ( !(parameter & 2) ) {
            	fs_dir_t_init(&my_dir);
            	if ( !(parameter & 1) ) {
            	   opendir_rc = fs_opendir(&my_dir, full_path);
            	} else {
            	   opendir_rc = fs_opendir(&my_dir, token_param1);
            	}
            } else {
            	alternate_ptr = strtok_r(NULL, "_", &choice_ptr);
            	if ( !(parameter & 1) ) { 
            	   opendir_rc = fs_opendir(alternate_ptr, full_path);	
            	} else {
            	   opendir_rc = fs_opendir(alternate_ptr, token_param1);
            	}
            }
            
            
            if (opendir_rc == 0) {
                printk("SUCCESS: opendir.\n");
            } else {
                printk("FAILURE: opendir, Error code: %d.\n", opendir_rc);
            }
            
            break;


        case 8: /* --- READDIR OPERATION --- */
            /* Expected structure: 8,<parameter>,<entry-ptr> */
            token_param1 = strtok_r(NULL, ",", &state_ptr);
            
            
            if ( !token_param1 ) {
                printk("Error: readdir entry absent.\n");
                break;
            }
            
            // If LSB of parameter is 0, tokens are properly processed
	        // If 2nd LSB of parameter is 0, a working directory pointer is passed
            if ( !(parameter & 2) ) {
            	if ( !(parameter & 1) ) {
            	   readdir_rc = fs_readdir(&my_dir, &my_dirent);
            	} else {
            	   readdir_rc = fs_readdir(&my_dir, token_param1);
            	}
            } else {
            	alternate_ptr = strtok_r(NULL, "_", &choice_ptr);
            	if ( !(parameter & 1) ) { 
            	   readdir_rc = fs_readdir(alternate_ptr, &my_dirent);	
            	} else {
            	   readdir_rc = fs_readdir(alternate_ptr, token_param1);
            	}
            }
            
            
            if (readdir_rc == 0) {
                printk("SUCCESS: readdir.\n");
            } else {
                printk("FAILURE: readdir, Error code: %d.\n", readdir_rc);
            }
            
            break;


        case 9: /* --- CLOSEDIR OPERATION --- */
            /* Expected structure: 9,<parameter> */
            
            // If 2nd LSB of parameter is 0, a working directory pointer is passed
            if ( !(parameter & 2) ) {
                closedir_rc = fs_closedir(&my_dir);
            } else {
                alternate_ptr = strtok_r(NULL, "_", &choice_ptr);
                closedir_rc = fs_closedir(alternate_ptr);
            }
            
            if (closedir_rc == 0) {
                printk("SUCCESS: closedir.\n");
            } else {
                printk("FAILURE: closedir, Error code: %d.\n", closedir_rc);
            }
            
            break;


        case 10: /* --- UNLINK OPERATION --- */
            /* Expected structure: 10,<parameter>,<f_path> */
            token_param1 = strtok_r(NULL, ",", &state_ptr);
            
            
            if (!token_param1) {
                printk("Error: unlink path absent.\n");
                break;
            }

            // If LSB of parameter is 0, tokens are properly processed
            if ( !(parameter & 1) ) {
            	prepend_mount_point(full_path, token_param1);
                unlink_rc = fs_unlink(full_path);
       	    } else {
                unlink_rc = fs_unlink(token_param1);
            }

            if (unlink_rc == 0) {
                printk("SUCCESS: unlink.\n");
            } else {
                printk("FAILURE: unlink, Error code: %d.\n", unlink_rc);
            }

            break;


        case 11: /* --- RENAME OPERATION --- */
            /* Expected structure: 11,<parameter>,<from_path>,<to_path> */
            token_param1 = strtok_r(NULL, ",", &state_ptr);
            token_param2 = strtok_r(NULL, ",", &state_ptr);
            
            
            if ((!token_param1) || (!token_param2)) {
                printk("Error: rename from path or to path absent.\n");
                break;
            }

            // If LSB of parameter is 0, tokens are properly processed
            if ( !(parameter & 1) ) {
            	prepend_mount_point(full_path, token_param1);
                prepend_mount_point(to_path, token_param2);
                // printk("From path: %s; To path: %s.\n", full_path, to_path);
                rename_rc = fs_rename(full_path, to_path);
       	    } else {
                rename_rc = fs_rename(token_param1, token_param2);
            }

            if (rename_rc == 0) {
                printk("SUCCESS: File renamed.\n");
            } else {
                printk("FAILURE: File rename, Error code: %d.\n", rename_rc);
            }

            break;


        case 12: /* --- TRUNCATE OPERATION --- */
            /* Expected structure: 12,<parameter>,<new-len> */
            token_param1 = strtok_r(NULL, ",", &state_ptr);
            
            if (!token_param1) {
                printk("Error: truncate length absent.\n");
                break;
            }

            // If LSB of parameter is 0, tokens are properly processed
            if ( !(parameter & 1) ) {
            	truncate_len = (off_t)strtoul(token_param1, NULL, 10);
       	    }
            
            // If 2nd LSB of parameter is 0, a working file pointer is passed
            if ( !(parameter & 2) ) {
                if ( !(parameter & 1) ) {
                    truncate_rc = fs_truncate(&my_file, truncate_len);
                } else {
                    truncate_rc = fs_truncate(&my_file, *token_param1);
                }
            } else {
                alternate_ptr = strtok_r(NULL, "_", &choice_ptr);
                if ( !(parameter & 1) ) {
                    truncate_rc = fs_truncate(alternate_ptr, truncate_len);
                } else {
                    truncate_rc = fs_truncate(alternate_ptr, *token_param1);
                }
            }

            if (truncate_rc == 0) {
                printk("SUCCESS: File truncated.\n");
            } else {
                printk("FAILURE: File truncate failed, Error code: %d.\n", truncate_rc);
            }

            break;


        case 13: /* --- SYNC OPERATION --- */
            /* Expected structure: 13,<parameter> */
            
            // If 2nd LSB of parameter is 0, a working file pointer is passed
            if ( !(parameter & 2) ) {
                sync_rc = fs_sync(&my_file);
            } else {
                alternate_ptr = strtok_r(NULL, "_", &choice_ptr);
                sync_rc = fs_sync(alternate_ptr);
            }
            
            if (sync_rc == 0) {
                printk("SUCCESS: File synced.\n");
            } else {
                printk("FAILURE: File not synced, Error code: %d.\n", sync_rc);
            }
            
            break;


        case 14: /* --- MOUNT OPERATION --- */
            /* Expected structure: 14,<parameter> */
            
            // If 2nd LSB of parameter is 0, a working mount pointer is passed
            if ( !(parameter & 2) ) {
                mount_rc = fs_mount(auto_mount_point);
            } else {
                alternate_ptr = strtok_r(NULL, "_", &choice_ptr);
                mount_rc = fs_mount(alternate_ptr);
            }
            
            if (mount_rc == 0) {
                printk("SUCCESS: File system mounted.\n");
            } else {
                printk("FAILURE: File system not mounted, Error code: %d.\n", mount_rc);
            }
            
            break;


        case 15: /* --- UNMOUNT OPERATION --- */
            /* Expected structure: 15,<parameter> */
            
            // If 2nd LSB of parameter is 0, a working mount pointer is passed
            if ( !(parameter & 2) ) {
                unmount_rc = fs_unmount(auto_mount_point);
            } else {
                alternate_ptr = strtok_r(NULL, "_", &choice_ptr);
                unmount_rc = fs_unmount(alternate_ptr);
            }
            
            if (unmount_rc == 0) {
                printk("SUCCESS: File system unmounted.\n");
            } else {
                printk("FAILURE: File system not unmounted, Error code: %d.\n", unmount_rc);
            }
            
            break;


        case 16: /* --- READMOUNT OPERATION --- */
            /* Expected structure: 16,<parameter>,<index> */
            token_param1 = strtok_r(NULL, ",", &state_ptr);
            
            
            if (!token_param1) {
                printk("Error: readmount index absent.\n");
                break;
            }

            // If LSB of parameter is 0, tokens are properly processed
            if ( !(parameter & 1) ) {
            	readmount_index = (int)strtoul(token_param1, NULL, 10);
                readmount_rc = fs_readmount(&readmount_index, &full_path);
       	    }
            else {
                readmount_rc = fs_readmount(token_param1, &full_path);
            }

            if (readmount_rc == 0) {
                printk("SUCCESS: readmount.\n");
            } else {
                printk("FAILURE: readmount, Error code: %d.\n", readmount_rc);
            }

            break;


        case 17: /* --- STAT OPERATION --- */
            /* Expected structure: 17,<parameter>,<path>,<file-status> */
            token_param1 = strtok_r(NULL, ",", &state_ptr);
            token_param2 = strtok_r(NULL, ",", &state_ptr);
            
            
            if ((!token_param1) || (!token_param2)) {
                printk("Error: stat path or status absent.\n");
                break;
            }

            // If LSB of parameter is 0, tokens are properly processed
            if ( !(parameter & 1) ) {
            	prepend_mount_point(full_path, token_param1);
                stat_rc = fs_stat(full_path, &my_dirent);
       	    } else {
                stat_rc = fs_stat(token_param1, token_param2);
            }

            if (stat_rc == 0) {
                printk("SUCCESS: File stat gathered.\n");
            } else {
                printk("FAILURE: File stat unavailable, Error code: %d.\n", stat_rc);
            }

            break;


        case 18: /* --- STATVFS OPERATION --- */
            /* This operation has serious problems. It returns SUCCESS even when bad 
            path is provided in the first argument, as long as the mount point is provided
            correctly (check 18,0,/RAM:/rolf,spandan). Sometimes it does not even care 
            about the second argument and returns SUCCESS (check 18,1,/RAM:/,spandan). */
            /* Expected structure: 18,<parameter>,<path>,<mount-status> */
            token_param1 = strtok_r(NULL, ",", &state_ptr);
            token_param2 = strtok_r(NULL, ",", &state_ptr);
            
            
            if ((!token_param1) || (!token_param2)) {
                printk("Error: statvfs path or status absent.\n");
                break;
            }

            // If LSB of parameter is 0, tokens are properly processed
            if ( !(parameter & 1) ) {
                prepend_mount_point(full_path, token_param1);
                // printk("FILE path set at: %s.\n",full_path);
                statvfs_rc = fs_statvfs(full_path, &my_statvfs);
       	    } else {
                statvfs_rc = fs_statvfs(token_param1, token_param2);
            }

            if (statvfs_rc == 0) {
                printk("SUCCESS: Mount statvfs gathered.\n");
            } else {
                printk("FAILURE: Mount statvfs unavailable, Error code: %d.\n", statvfs_rc);
            }

            break;


        default:
            printk("Unrecognized Operation ID: %d.\n", operation_choice);
            break;
    }
}


int main(void)
{
    if (!device_is_ready(uart_dev)) {
        printk("Error: UART peripheral device not ready\n");
        return -1;
    }

    printk("CHERI-Zephyr Polling UART Active. Type text and hit Enter...\n");

while (1) {
        unsigned char c;

        if (uart_poll_in(uart_dev, &c) == 0) {

            /* --- 1. HANDLE BACKSPACE (ASCII 8 or 127) --- */
            if (c == '\b' || c == 0x7F) {
                if (rx_idx > 0) {
                    rx_idx--; /* Delete last character from buffer */

                    /* Visual erase sequence: Move back, overwrite with space, move back */
                    // uart_poll_out(uart_dev, '\b');
                    // uart_poll_out(uart_dev, ' ');
                    // uart_poll_out(uart_dev, '\b');
                }
                continue; /* Skip normal echo and processing */
            }

            /* --- 2. ECHO ALL OTHER CHARACTERS --- */
            // uart_poll_out(uart_dev, c);

            // Visual feedback not required for AFL++

            /* Process character if it isn't a string or line terminator */
            if (c != '\0' && c != '\n' && c != '\r') {
                /* Safe capability bounds protection check */
                if (rx_idx < BUFFER_SIZE) {
                    rx_buf[rx_idx++] = (char)c;
                }
            }

            /* Print trigger: Found '\0', '\r', '\n', or buffer hit max size limit */
            if (c == '\0' || c == '\n' || c == '\r' || rx_idx >= BUFFER_SIZE) {

                /* Only process and print if we actually collected characters */
                if (rx_idx > 0) {
                    rx_buf[rx_idx] = '\0'; /* Guard trailing slice */

                    // Initialize buffers to zero to avoid residual data issues
                    memset(full_path, 0, (BUFFER_SIZE + 10));
                    memset(to_path, 0, (BUFFER_SIZE + 10));

                    // File operation
                    process_file_operation(rx_buf);
                    printk("----------\n\n");

                    /* Reset state mapping */
                    rx_idx = 0;
                    memset(rx_buf, 0, sizeof(rx_buf));
                }
            }
        } else {
            /* No data in hardware FIFO; sleep to prevent CPU starvation */
            k_msleep(1);
        }
    }
}



