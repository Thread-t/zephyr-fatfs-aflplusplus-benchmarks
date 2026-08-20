#!/bin/bash
OP=$1      # e.g. 01 for write, 08 for mount_raw
QDIR=$2    # e.g. ~/fuzz_fatfs/out_write/default/queue

VP=/home/cheri/riscv-vp/vp/build/bin/qemu32-vp
ELF=~/CHERI-zephyr/samples/fatfs_fuzz_uart/build_write/zephyr/zephyr.elf
RESULTS=~/fuzz_fatfs/uart_results_op${OP}.csv
echo "file,outcome" > "$RESULTS"

for f in "$QDIR"/id:*; do
    [ -f "$f" ] || continue
    NAME=$(basename "$f")
    LOG=~/fuzz_fatfs/qemu_replay_logs/uart_${OP}_${NAME}.log

    python3 ~/fuzz_fatfs/send_packet.py "$OP" fuzz.txt "$f" | \
        timeout 5 stdbuf -oL -eL "$VP" "$ELF" > "$LOG" 2>&1
    stty sane 2>/dev/null

    if grep -qE "CHERI exception|ZEPHYR FATAL ERROR" "$LOG"; then
        OUT="CHERI_FAULT_CAUGHT"
    elif grep -q "OP_DONE" "$LOG"; then
        OUT="CLEAN"
    else
        OUT="STUCK"
    fi

    echo "$NAME,$OUT" >> "$RESULTS"
    echo "[$OUT] $NAME"
done
echo "Done → $RESULTS"
