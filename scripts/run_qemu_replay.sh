#!/bin/bash
mkdir -p ~/fuzz_fatfs/qemu_replay_logs
RESULTS=~/fuzz_fatfs/qemu_replay_results.csv
echo "input_file,exit_status,outcome" > "$RESULTS"

VP_BIN=/home/cheri/riscv-vp/vp/build/bin/qemu32-vp
BUILD_DIR=~/CHERI-zephyr/samples/fatfs_shell/build_fuzz
ELF="$BUILD_DIR/zephyr/zephyr.elf"
FUZZ_H=~/CHERI-zephyr/samples/fatfs_shell/src/fuzz_input.h

for f in "$@"; do
    [ -f "$f" ] || continue
    NAME=$(basename "$f")
    LOG="$HOME/fuzz_fatfs/qemu_replay_logs/${NAME}.log"
    : > "$LOG"

    xxd -i "$f" > "$FUZZ_H"
    sed -i 's/unsigned char [A-Za-z0-9_]*\[\]/unsigned char fuzz_data[]/' "$FUZZ_H"
    sed -i 's/unsigned int [A-Za-z0-9_]*_len/unsigned int fuzz_data_len/' "$FUZZ_H"

    (cd "$BUILD_DIR" && ninja >/dev/null 2>>"$LOG")

    sleep 1; timeout -k 3 10 stdbuf -oL -eL "$VP_BIN" "$ELF" >> "$LOG" 2>&1; pkill -9 -f qemu32-vp 2>/dev/null
    STATUS=$?
    stty sane 2>/dev/null   # reset terminal after each raw-mode UART session

    if grep -qE "CHERI exception|ZEPHYR FATAL ERROR|CPU CHERI hardware exception" "$LOG"; then
        OUTCOME="CHERI_FAULT_CAUGHT"
    elif grep -q "REPLAY_DONE" "$LOG"; then
        OUTCOME="CLEAN"
    else
        OUTCOME="STUCK_BEFORE_REPLAY_DONE"
    fi

    echo "$NAME,$STATUS,$OUTCOME" >> "$RESULTS"
    echo "[$OUTCOME] $NAME"
done

echo "Done. Summary in $RESULTS"
