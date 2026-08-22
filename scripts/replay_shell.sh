#!/bin/bash
OP=$1
QDIR=$2
RESULTS=~/fuzz_fatfs/shell_results_${OP}.csv
LOGDIR=~/fuzz_fatfs/shell_logs_${OP}

mkdir -p "$LOGDIR"
echo "file,outcome" > "$RESULTS"

for f in "$QDIR"/id:*; do
    [ -f "$f" ] || continue
    NAME=$(basename "$f")
    LOG="$LOGDIR/${NAME}.log"

    # get the shell commands for this input
    CMDS=$(python3 ~/fuzz_fatfs/input_to_shell_cmd.py "$OP" "$f" 2>/dev/null)
    CMD1=$(echo "$CMDS" | sed -n '1p')
    CMD2=$(echo "$CMDS" | sed -n '2p')

    # run via expect — handles terminal modes cleanly
    ~/fuzz_fatfs/replay_one.exp "$CMD1" "$CMD2" > "$LOG" 2>&1
    stty sane 2>/dev/null
    sleep 0.3

    if grep -qE "CHERI exception|ZEPHYR FATAL ERROR|CPU CHERI hardware exception" "$LOG"; then
        OUT="CHERI_FAULT_CAUGHT"
    elif grep -qE "uart:~\\\$" "$LOG"; then
        OUT="CLEAN"
    else
        OUT="STUCK"
    fi

    echo "$NAME,$OUT" >> "$RESULTS"
    echo "[$OUT] $NAME"
done

echo ""
echo "=== Summary for op=$OP ==="
cut -d, -f2 "$RESULTS" | sort | uniq -c
