# CHERI-Zephyr FatFs Fuzzing Harness

Author: Sayak Deb

This project fuzzes the FatFs (`ff.c`) implementation used by the
CHERI-Zephyr build. It first uses AFL++ on a fast, host-native harness to
find memory-safety bugs, then replays the interesting inputs against the
real CHERI-aware RISC-V virtual platform (VP) to check whether the CHERI
hardware itself catches the resulting violations.

---

## Problem Statement

Can the CHERI hardware inside our custom RISC-V VP detect memory-safety
violations in the FatFS filesystem when it processes malformed or
adversarial inputs?

---

## Phase 1 — Host-side fuzzing (AFL++)

### 1. Install AFL++

```bash
sudo apt-get update
sudo apt-get install -y build-essential python3-dev automake cmake git \
    flex bison libglib2.0-dev libpixman-1-dev python3-setuptools \
    llvm clang mtools dosfstools
# mtools/dosfstools give us mkfs.vfat and mcopy for building FAT seed images

cd ~
git clone https://github.com/AFLplusplus/AFLplusplus.git
cd AFLplusplus
make source-only
# builds afl-fuzz + afl-clang-fast (source-instrumented fuzzing); skips
# QEMU/Unicorn binary-only modes we don't need for this approach
sudo make install

afl-fuzz --version
which afl-clang-fast
# sanity check that AFL++ is correctly installed
```

### 2. Identify the exact FatFs source/config the target build uses

```bash
cd ~/CHERI-zephyr/build
grep -o '"[^"]*ff\.c"' compile_commands.json
# confirms which of the two ff.c candidates (modules/fatfs vs modules/fs/fatfs)
# is actually compiled into zephyr.elf, and shows the full compile invocation

grep FATFS build_filesystem/zephyr/include/generated/zephyr/autoconf.h
# extracts the resolved Kconfig values (FF_FS_FATFS_* settings) that the
# devicetree-dependent zephyr_fatfs_config.h would otherwise compute at
# Zephyr-build time, so we can hardcode the same values standalone

cat samples/fatfs_shell/fatfs_fstab.overlay
# confirms the real disk name ("RAM"), sector size (512), and sector count
# (128 => 64KB total) so our fake disk in the harness matches the real one
```

### 3. Build a standalone (host-native) fuzzing harness around the real `ff.c`

```bash
mkdir -p ~/fuzz_fatfs/src ~/fuzz_fatfs/corpus ~/fuzz_fatfs/out
cp ~/CHERI-zephyr/modules/fs/fatfs/ff.c ~/fuzz_fatfs/src/
cp ~/CHERI-zephyr/modules/fs/fatfs/include/ff.h ~/fuzz_fatfs/src/
cp ~/CHERI-zephyr/modules/fs/fatfs/include/ffconf.h ~/fuzz_fatfs/src/
cp ~/CHERI-zephyr/modules/fs/fatfs/include/diskio.h ~/fuzz_fatfs/src/
# pull the exact FatFs source/config the real firmware links against, so
# bugs found are representative of the actual target, not generic FatFs
# (created ffconf_fuzz_override.h, diskio_fuzz.c, harness.c — hardcode the
#  resolved FF_* config values, stub a fake in-memory "disk" backed by AFL's
#  input bytes, and drive f_mount/f_opendir/f_read/f_write/f_mkdir in a loop)

cd ~/fuzz_fatfs/src
afl-clang-fast -g -O1 -fsanitize=address,undefined \
    -DZEPHYR_CONFIG_OVERRIDE=ffconf_fuzz_override.h \
    -I. ff.c diskio_fuzz.c harness.c -o ../fuzz_fatfs
# compiles with AFL coverage instrumentation + ASan/UBSan for crash detection
```

**Why separate harnesses per operation**

Each harness targets a different code path in `ff.c` — one harness cannot
cover all of them effectively:

| Harness | Exercises |
|---|---|
| `harness_write` | `f_open`, `f_write`, `f_close` |
| `harness_read` | `f_open`, `f_read`, `f_lseek` |
| `harness_mount` | `f_mount`, `f_opendir`, `f_readdir` |
| `harness_mkdir` | `f_mkdir`, directory allocation |

Each iteration: raw bytes in → load as disk image → call fs functions →
unmount → repeat.

### 4. Build seed corpus

```bash
cd ~/fuzz_fatfs
dd if=/dev/zero of=corpus/seed1.img bs=1k count=64
mkfs.vfat -F 12 -n TEST corpus/seed1.img
# a valid empty FAT12 image sized to match the real 64KB RAM disk, giving
# AFL a structurally-correct starting point instead of raw random bytes

dd if=/dev/zero of=/tmp/seed2.img bs=1k count=64
mkfs.vfat -F 12 -n TEST2 /tmp/seed2.img
mcopy -i /tmp/seed2.img ~/.bashrc ::hello.txt
cp /tmp/seed2.img corpus/seed2.img
# a second seed that also contains a file, so the read path is exercised
# from the very first generation
```

### 5. Sanity check, then fuzz

```bash
./fuzz_fatfs < corpus/seed1.img
# confirms the harness runs cleanly on a valid input before fuzzing

afl-fuzz -i corpus -o out -- ./fuzz_fatfs
# coverage-guided fuzzing run; AFL mutates the seed images and feeds them
# through f_mount/f_opendir/f_read/f_write, flagging ASan/UBSan crashes
```

**How AFL++'s coverage-guided fuzzing works**

1. Run a seed through the harness and record which lines of `ff.c`
   executed ("coverage bitmap" — a map of code branches hit).
2. Mutate the input: flip bits, corrupt field values, change sector
   counts, splice two inputs together.
3. Run the mutated input — did it reach new lines of `ff.c` not seen
   before?
   - Yes → save to `queue/`, mutate further.
   - No → discard, try another mutation.
4. If the harness crashes (ASan fires) → save to `crashes/`.
5. If the harness loops forever (timeout) → save to `hangs/`.
6. Repeat at 100,000+ iterations per second.

Coverage guidance matters because blind random mutation would never find
things like the boot-sector checksum validation or cluster-chain walking
code deep inside `f_mount()`. AFL++ uses coverage feedback to steer
mutations toward exactly those hard-to-reach paths.

### 6. Triage

```bash
ls out/default/crashes/
for f in out/default/crashes/id:*; do
    echo "=== $f ==="
    ./fuzz_fatfs < "$f" 2>&1 | grep -iE "runtime error|ERROR: |SUMMARY:|Assertion" | head -5
done
# replays each crash to see the sanitizer's actual diagnostic and bug class
```

---

## Phase 2 — Replaying findings on the CHERI target

Once AFL++ has built up a corpus of interesting/crashing inputs on the
host, those same byte sequences are replayed against the real
CHERI-Zephyr firmware to see whether the CHERI hardware itself catches
the violation.

### `input_to_shell_cmd.py` — the translator

The fuzzer produces raw binary files, but the CHERI target only accepts
text shell commands at a UART prompt, so something has to bridge the two:

- `byte[0]` → controls filename length (0–11 chars)
- `bytes[1..N]` → become the filename (sanitized to printable ASCII)
- `bytes[N+1..]` → become the data payload (hex-encoded)

Example output:

```
fs write /RAM:/fuzz.txt 0 eb3c906d
fs read /RAM:/fuzz.txt
```

This means the same byte sequence AFL used to stress `ff.c` on the host
now becomes a real shell interaction that stresses the exact same code
paths on the CHERI target.

### `replay_one.exp` — the expect script

Automates sending commands to the interactive Zephyr shell. A simple pipe
doesn't work here because the VP's UART behaves like a real serial
terminal with raw mode — piping text directly breaks the terminal state
between runs, and the RX buffer overflows if data arrives too fast.

What it does:

1. Launches `qemu32-vp` with the CHERI firmware ELF.
2. Waits for `uart:~$` (Zephyr shell ready).
3. Sends the command string slowly (character by character).
4. Waits for `uart:~$` again (command finished).
5. Sends Ctrl+C to kill QEMU cleanly.

This handles the timing, terminal modes, and buffer management that a raw
pipe can't.

### `replay_shell.sh` — the batch orchestrator

Runs every AFL-found input through the CHERI target without recompiling,
and records the results.

For each file in AFL's `queue/`:

1. `input_to_shell_cmd.py` → shell command text.
2. `replay_one.exp` → boot CHERI VP → send command → capture output.
3. Grep output for `CHERI exception` / `ZEPHYR FATAL ERROR`.
4. Classify as:
   - `CHERI_FAULT_CAUGHT` — CHERI hardware detected a violation.
   - `CLEAN` — completed without fault.
   - `STUCK` — VP didn't respond / timed out.
5. Write to `shell_results_<op>.csv`.
6. Kill the VP, sleep briefly, move to the next file.

Output: a CSV table showing a per-input classification across the entire
fuzzer corpus.

---

## Full Pipeline

```
Phase 1: HOST (fast — 100k+ inputs/sec, ASAN catches bugs)

  seed1.img ──┐
  seed2.img ──┤
              ▼
         AFL++ fuzzer
              │ mutates bytes, tracks ff.c coverage
              ▼
  harness_write → out_write/queue/ (write-path inputs)
  harness_read  → out_read/queue/  (read-path inputs)
  harness_mount → out_mount/queue/ (mount-path inputs)
  harness_mkdir → out_mkdir/queue/ (mkdir-path inputs)

  → Any ASAN crash = memory bug confirmed in ff.c


Phase 2: CHERI TARGET (slow — one VP boot per input)

  out_write/queue/id:000042  (one AFL-found byte sequence)
         │
         ▼
  input_to_shell_cmd.py
  "eb3c90..." → "fs write /RAM:/fuzz.txt 0 eb3c90"
         │
         ▼
  replay_one.exp
  boots: qemu32-vp zephyr.elf (CHERI-aware RISC-V VP)
  sends: "fs write /RAM:/fuzz.txt 0 eb3c90"
  via:   Zephyr UART shell (same interface a real user would use)
         │
         ├── Zephyr calls: fs_open() → fs_write() → FatFS internals
         │
         ├── CLEAN: command completes, "uart:~$" returns
         │
         └── CHERI_FAULT_CAUGHT:
             "[ISS] x10 took CHERI exception code Tag Violation"
             "mcause: 28, CHERI exception"
             ">>> ZEPHYR FATAL ERROR 0: CPU exception"
             → CHERI hardware stopped a capability violation
         │
         ▼
  shell_results_write.csv
  shell_results_read.csv
  shell_results_mount.csv
```
