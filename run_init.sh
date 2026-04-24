#!/bin/sh

cd /userdata

LOG_TIME=$(date +%Y%m%d_%H%M%S)
LOG_FILE="/userdata/log/boot.log"
OLD_LOG="/userdata/log/boot.log.old"

KEEP_LINES=1000
CHECK_INTERVAL=30 # sec

if [ -f "$LOG_FILE" ]; then
    cp "$LOG_FILE" "$OLD_LOG"
    true > "$LOG_FILE"
    echo "Previous log backed up to $OLD_LOG"
fi

echo "--- Boot at $LOG_TIME ---" > "$LOG_FILE"

chmod +x /userdata/rv1106_ipc_custom
/userdata/rv1106_ipc_custom 2>&1 | grep -v "get_af_stats" | grep -v "\"@" >> "$LOG_FILE" &

(
    while true; do
        sleep $CHECK_INTERVAL 
        if [ -f "$LOG_FILE" ]; then
            LINE_COUNT=$(wc -l < "$LOG_FILE")
            if [ "$LINE_COUNT" -gt $KEEP_LINES ]; then
                tail -n $KEEP_LINES "$LOG_FILE" > "${LOG_FILE}.tmp"
                cat "${LOG_FILE}.tmp" > "$LOG_FILE"
                rm "${LOG_FILE}.tmp"
                echo "[$(date)] Log truncated to $KEEP_LINES lines" >> "$LOG_FILE"
            fi
        fi
    done
) &