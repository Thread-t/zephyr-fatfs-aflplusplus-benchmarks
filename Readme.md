/#Author : Sayak Deb

# --- Install AFL++ ---
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


# --- Identify the exact FatFs source/config our target build uses ---
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


# --- Build a standalone (host-native) fuzzing harness around the real ff.c ---
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


# --- Build seed corpus ---
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


# --- Sanity check, then fuzz ---
./fuzz_fatfs < corpus/seed1.img
# confirms the harness runs cleanly on a valid input before fuzzing

afl-fuzz -i corpus -o out -- ./fuzz_fatfs
# coverage-guided fuzzing run; AFL mutates the seed images and feeds them
# through f_mount/f_opendir/f_read/f_write, flagging ASan/UBSan crashes


# --- Triage ---
ls out/default/crashes/
for f in out/default/crashes/id:*; do
    echo "=== $f ==="
    ./fuzz_fatfs < "$f" 2>&1 | grep -iE "runtime error|ERROR: |SUMMARY:|Assertion" | head -5
done
# replays each crash to see the sanitizer's actual diagnostic and bug class
