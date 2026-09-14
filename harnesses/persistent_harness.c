/*
   american fuzzy lop++ - persistent mode example
   --------------------------------------------

   Originally written by Michal Zalewski

   Copyright 2015 Google Inc. All rights reserved.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at:

     http://www.apache.org/licenses/LICENSE-2.0

   This file demonstrates the high-performance "persistent mode" that may be
   suitable for fuzzing certain fast and well-behaved libraries, provided that
   they are stateless or that their internal state can be easily reset
   across runs.

   To make this work, the library and this shim need to be compiled in LLVM
   mode using afl-clang-fast (other compiler wrappers will *not* work).

 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

__AFL_FUZZ_INIT();

// Global trackers for the child process and pipes
pid_t child_pid = 0;
int pipe_in[2] = {-1, -1};
int pipe_out[2] = {-1, -1};
// Let AFL++ know about the different outputs
extern uint8_t *__afl_area_ptr;

// Discard OS greeting messages from the target program
void discard() {
    char discard_buf[1024];
    ssize_t bytes_read = read(pipe_out[0], discard_buf, (sizeof(discard_buf) - 1));
    if (bytes_read > 0) {
        discard_buf[bytes_read] = '\0'; // Null-terminate the buffer
        // printf("Discarded output:\n%s\n", discard_buf);
    }
}

// Helper function to safely spin up a clean copy of your_program
void spawn_target() {
    // If an old target is hanging around, kill it cleanly
    if (child_pid > 0) {
        kill(child_pid, SIGKILL);
        waitpid(child_pid, NULL, 0);
    }
    // Close old pipes if they exist
    if (pipe_in[0] != -1) { close(pipe_in[0]); close(pipe_in[1]); }
    if (pipe_out[0] != -1) { close(pipe_out[0]); close(pipe_out[1]); }

    if (pipe(pipe_in) == -1 || pipe(pipe_out) == -1) {
        perror("Failed to create fresh pipelines");
        exit(1);
    }

    child_pid = fork();
    if (child_pid == 0) {
        // --- CHILD PROCESS ---
        dup2(pipe_in[0], STDIN_FILENO);
        dup2(pipe_out[1], STDOUT_FILENO);
        // dup2(pipe_out[1], STDERR_FILENO);

        close(pipe_in[0]); close(pipe_in[1]);
        close(pipe_out[0]); close(pipe_out[1]);

        execl("/home/spandan/qemu32-vp/riscv-vp/vp/build_QEMU/bin/qemu32-vp", \
              "--intercept-syscalls", \
              "/home/spandan/ZEPHYR-workspace/CHERI-zephyr/build_afl_plus_plus/zephyr/zephyr.elf", \
               NULL);
        perror("Failed to execute target program");
        exit(1);
    }

    // --- PARENT PROCESS ---
    close(pipe_in[0]);  // Close child's read side
    close(pipe_out[1]); // Close child's write side

    // Set output pipe to non-blocking mode
    int flags = fcntl(pipe_out[0], F_GETFL, 0);
    fcntl(pipe_out[0], F_SETFL, flags | O_NONBLOCK);

    // Wait briefly and discard boot output
    usleep(350000); // 350ms
    discard();
}

int main() {

    #ifdef __AFL_HAVE_MANUAL_CONTROL
    __AFL_INIT();
    #endif

    unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF;
    // unsigned char buf[2048];
    char log_read_buffer[2048];

    // Boot up the target executable the very first time
    spawn_target();

    while (__AFL_LOOP(1000)) {
    // while (1) {
        int len = __AFL_FUZZ_TESTCASE_LEN;
        // int len = read(STDIN_FILENO, buf, (sizeof(buf) - 1));
        // buf[len] = '\0'; // Null-terminate the input buffer

        // 1. Double check: Did the target crash out entirely on the *previous* round?
        int status;
        pid_t result = waitpid(child_pid, &status, WNOHANG);
        if (result != 0) { 
            // Target is dead or terminated! Spawn a clean one before proceeding.
            spawn_target();
        }

        if (len > 0) {
            // 2. Feed input
            write(pipe_in[1], buf, len);
            write(pipe_in[1], "\0", 1); 
            
            usleep(240000); // Small processing delay window

            // 3. Scan output strings for the hidden CHERI crashes
            memset(log_read_buffer, 0, sizeof(log_read_buffer));
            ssize_t bytes_logged = read(pipe_out[0], log_read_buffer, sizeof(log_read_buffer) - 1);

            if (bytes_logged > 0) {
                if (strstr(log_read_buffer, "operation mode") != NULL) {
                    /*printf("Operation mode.\n");*/
                    if (__afl_area_ptr[100] < 255) __afl_area_ptr[100]++;
                    else if (__afl_area_ptr[101] < 255) __afl_area_ptr[101]++;
                    else if (__afl_area_ptr[102] < 255) __afl_area_ptr[102]++; 
                    else if (__afl_area_ptr[103] < 255) __afl_area_ptr[103]++;
                }
                
                if (strstr(log_read_buffer, "Parameter choice") != NULL) {
                    /*printf("Parameter choice.\n");*/
                    if (__afl_area_ptr[104] < 255) __afl_area_ptr[104]++;
                    else if (__afl_area_ptr[105] < 255) __afl_area_ptr[105]++;
                    else if (__afl_area_ptr[106] < 255) __afl_area_ptr[106]++; 
                    else if (__afl_area_ptr[107] < 255) __afl_area_ptr[107]++;
                }

                if (strstr(log_read_buffer, "open") != NULL) {
                    /*printf("Open.\n");*/
                    if (__afl_area_ptr[108] < 255) __afl_area_ptr[108]++;
                    else if (__afl_area_ptr[109] < 255) __afl_area_ptr[109]++;
                    else if (__afl_area_ptr[110] < 255) __afl_area_ptr[110]++; 
                    else if (__afl_area_ptr[111] < 255) __afl_area_ptr[111]++;
                }

                if (strstr(log_read_buffer, "close") != NULL) {
                    /*printf("Close.\n");*/
                    if (__afl_area_ptr[112] < 255) __afl_area_ptr[112]++;
                    else if (__afl_area_ptr[113] < 255) __afl_area_ptr[113]++;
                    else if (__afl_area_ptr[114] < 255) __afl_area_ptr[114]++; 
                    else if (__afl_area_ptr[115] < 255) __afl_area_ptr[115]++;
                }

                if (strstr(log_read_buffer, "write") != NULL) {
                    /*printf("Write.\n");*/
                    if (__afl_area_ptr[116] < 255) __afl_area_ptr[116]++;
                    else if (__afl_area_ptr[117] < 255) __afl_area_ptr[117]++;
                    else if (__afl_area_ptr[118] < 255) __afl_area_ptr[118]++; 
                    else if (__afl_area_ptr[119] < 255) __afl_area_ptr[119]++;
                }

                if (strstr(log_read_buffer, "read") != NULL) {
                    /*printf("Read.\n");*/
                    if (__afl_area_ptr[120] < 255) __afl_area_ptr[120]++;
                    else if (__afl_area_ptr[121] < 255) __afl_area_ptr[121]++;
                    else if (__afl_area_ptr[122] < 255) __afl_area_ptr[122]++; 
                    else if (__afl_area_ptr[123] < 255) __afl_area_ptr[123]++;
                }

                if (strstr(log_read_buffer, "seek") != NULL) {
                    /*printf("Seek.\n");*/
                    if (__afl_area_ptr[124] < 255) __afl_area_ptr[124]++;
                    else if (__afl_area_ptr[125] < 255) __afl_area_ptr[125]++;
                    else if (__afl_area_ptr[126] < 255) __afl_area_ptr[126]++; 
                    else if (__afl_area_ptr[127] < 255) __afl_area_ptr[127]++;
                }

                if (strstr(log_read_buffer, "tell") != NULL) {
                    /*printf("Tell.\n");*/
                    if (__afl_area_ptr[128] < 255) __afl_area_ptr[128]++;
                    else if (__afl_area_ptr[129] < 255) __afl_area_ptr[129]++;
                    else if (__afl_area_ptr[130] < 255) __afl_area_ptr[130]++; 
                    else if (__afl_area_ptr[131] < 255) __afl_area_ptr[131]++;
                }

                if (strstr(log_read_buffer, "mkdir") != NULL) {
                    /*printf("Mkdir.\n");*/
                    if (__afl_area_ptr[132] < 255) __afl_area_ptr[132]++;
                    else if (__afl_area_ptr[133] < 255) __afl_area_ptr[133]++;
                    else if (__afl_area_ptr[134] < 255) __afl_area_ptr[134]++; 
                    else if (__afl_area_ptr[135] < 255) __afl_area_ptr[135]++;
                }

                if (strstr(log_read_buffer, "opendir") != NULL) {
                    /*printf("Opendir.\n");*/
                    if (__afl_area_ptr[136] < 255) __afl_area_ptr[136]++;
                    else if (__afl_area_ptr[137] < 255) __afl_area_ptr[137]++;
                    else if (__afl_area_ptr[138] < 255) __afl_area_ptr[138]++; 
                    else if (__afl_area_ptr[139] < 255) __afl_area_ptr[139]++;
                }

                if (strstr(log_read_buffer, "readdir") != NULL) {
                    /*printf("Readdir.\n");*/
                    if (__afl_area_ptr[140] < 255) __afl_area_ptr[140]++;
                    else if (__afl_area_ptr[141] < 255) __afl_area_ptr[141]++;
                    else if (__afl_area_ptr[142] < 255) __afl_area_ptr[142]++; 
                    else if (__afl_area_ptr[143] < 255) __afl_area_ptr[143]++;
                }

                if (strstr(log_read_buffer, "closedir") != NULL) {
                    /*printf("Closedir.\n");*/
                    if (__afl_area_ptr[144] < 255) __afl_area_ptr[144]++;
                    else if (__afl_area_ptr[145] < 255) __afl_area_ptr[145]++;
                    else if (__afl_area_ptr[146] < 255) __afl_area_ptr[146]++; 
                    else if (__afl_area_ptr[147] < 255) __afl_area_ptr[147]++;
                }

                if (strstr(log_read_buffer, "unlink") != NULL) {
                    /*printf("Unlink.\n");*/
                    if (__afl_area_ptr[148] < 255) __afl_area_ptr[148]++;
                    else if (__afl_area_ptr[149] < 255) __afl_area_ptr[149]++;
                    else if (__afl_area_ptr[150] < 255) __afl_area_ptr[150]++; 
                    else if (__afl_area_ptr[151] < 255) __afl_area_ptr[151]++;
                }

                if (strstr(log_read_buffer, "rename") != NULL) {
                    /*printf("Rename.\n");*/
                    if (__afl_area_ptr[152] < 255) __afl_area_ptr[152]++;
                    else if (__afl_area_ptr[153] < 255) __afl_area_ptr[153]++;
                    else if (__afl_area_ptr[154] < 255) __afl_area_ptr[154]++; 
                    else if (__afl_area_ptr[155] < 255) __afl_area_ptr[155]++;
                }

                if (strstr(log_read_buffer, "truncate") != NULL) {
                    /*printf("Truncate.\n");*/
                    if (__afl_area_ptr[156] < 255) __afl_area_ptr[156]++;
                    else if (__afl_area_ptr[157] < 255) __afl_area_ptr[157]++;
                    else if (__afl_area_ptr[158] < 255) __afl_area_ptr[158]++; 
                    else if (__afl_area_ptr[159] < 255) __afl_area_ptr[159]++;
                }

                if (strstr(log_read_buffer, "sync") != NULL) {
                    /*printf("Sync.\n");*/
                    if (__afl_area_ptr[160] < 255) __afl_area_ptr[160]++;
                    else if (__afl_area_ptr[161] < 255) __afl_area_ptr[161]++;
                    else if (__afl_area_ptr[162] < 255) __afl_area_ptr[162]++; 
                    else if (__afl_area_ptr[163] < 255) __afl_area_ptr[163]++;
                }

                if (strstr(log_read_buffer, "mount") != NULL) {
                    /*printf("Mount.\n");*/
                    if (__afl_area_ptr[164] < 255) __afl_area_ptr[164]++;
                    else if (__afl_area_ptr[165] < 255) __afl_area_ptr[165]++;
                    else if (__afl_area_ptr[166] < 255) __afl_area_ptr[166]++; 
                    else if (__afl_area_ptr[167] < 255) __afl_area_ptr[167]++;
                }

                if (strstr(log_read_buffer, "unmount") != NULL) {
                    /*printf("Unmount.\n");*/
                    if (__afl_area_ptr[168] < 255) __afl_area_ptr[168]++;
                    else if (__afl_area_ptr[169] < 255) __afl_area_ptr[169]++;
                    else if (__afl_area_ptr[170] < 255) __afl_area_ptr[170]++; 
                    else if (__afl_area_ptr[171] < 255) __afl_area_ptr[171]++;
                }

                if (strstr(log_read_buffer, "readmount") != NULL) {
                    /*printf("Readmount.\n");*/
                    if (__afl_area_ptr[172] < 255) __afl_area_ptr[172]++;
                    else if (__afl_area_ptr[173] < 255) __afl_area_ptr[173]++;
                    else if (__afl_area_ptr[174] < 255) __afl_area_ptr[174]++; 
                    else if (__afl_area_ptr[175] < 255) __afl_area_ptr[175]++;
                }

                if (strstr(log_read_buffer, "stat") != NULL) {
                    /*printf("Stat.\n");*/
                    if (__afl_area_ptr[176] < 255) __afl_area_ptr[176]++;
                    else if (__afl_area_ptr[177] < 255) __afl_area_ptr[177]++;
                    else if (__afl_area_ptr[178] < 255) __afl_area_ptr[178]++; 
                    else if (__afl_area_ptr[179] < 255) __afl_area_ptr[179]++;
                }

                if (strstr(log_read_buffer, "statvfs") != NULL) {
                    /*printf("Statvfs.\n");*/
                    if (__afl_area_ptr[180] < 255) __afl_area_ptr[180]++;
                    else if (__afl_area_ptr[181] < 255) __afl_area_ptr[181]++;
                    else if (__afl_area_ptr[182] < 255) __afl_area_ptr[182]++; 
                    else if (__afl_area_ptr[183] < 255) __afl_area_ptr[183]++;
                }

                if (strstr(log_read_buffer, "Unrecognized") != NULL) {
                    /*printf("Unrecognized.\n");*/
                    if (__afl_area_ptr[184] < 255) __afl_area_ptr[184]++;
                    else if (__afl_area_ptr[185] < 255) __afl_area_ptr[185]++;
                    else if (__afl_area_ptr[186] < 255) __afl_area_ptr[186]++; 
                    else if (__afl_area_ptr[187] < 255) __afl_area_ptr[187]++;
                }

                if (strstr(log_read_buffer, "Error") != NULL) {
                    /*printf("Error.\n");*/
                    if (__afl_area_ptr[188] < 255) __afl_area_ptr[188]++;
                    else if (__afl_area_ptr[189] < 255) __afl_area_ptr[189]++;
                    else if (__afl_area_ptr[190] < 255) __afl_area_ptr[190]++; 
                    else if (__afl_area_ptr[191] < 255) __afl_area_ptr[191]++;
                }

                if (strstr(log_read_buffer, "SUCCESS") != NULL) {
                    /*printf("Success.\n");*/
                    if (__afl_area_ptr[192] < 255) __afl_area_ptr[192]++;
                    else if (__afl_area_ptr[193] < 255) __afl_area_ptr[193]++;
                    else if (__afl_area_ptr[194] < 255) __afl_area_ptr[194]++; 
                    else if (__afl_area_ptr[195] < 255) __afl_area_ptr[195]++;
                }

                if (strstr(log_read_buffer, "FAILURE") != NULL) {
                    /*printf("Failure.\n");*/
                    if (__afl_area_ptr[196] < 255) __afl_area_ptr[196]++;
                    else if (__afl_area_ptr[197] < 255) __afl_area_ptr[197]++;
                    else if (__afl_area_ptr[198] < 255) __afl_area_ptr[198]++; 
                    else if (__afl_area_ptr[199] < 255) __afl_area_ptr[199]++;
                }

                if (strstr(log_read_buffer, "CHERI") != NULL && \
                    strstr(log_read_buffer, "exception") != NULL) {
                    // Kill vp before timeout
                    if (child_pid > 0) {
                        kill(child_pid, SIGKILL);
                        waitpid(child_pid, NULL, 0);
                    }
                    // Tell AFL++ we found a crash!
                    abort(); 
                }
            } else {
                // If we can't read anything, the target may have crashed or hung.
                if (child_pid > 0) {
                    kill(child_pid, SIGKILL);
                    waitpid(child_pid, NULL, 0);
                }
                // Sleep long enough to exceed the AFL++ execution timeout window.
                usleep(3000000);
            }
        }
    }

    // Final house cleaning
    if (child_pid > 0) {
        kill(child_pid, SIGKILL);
        waitpid(child_pid, NULL, 0);
    }
    return 0;
}
