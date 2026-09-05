#!/bin/sh
set -u

OUT="${1:-mbu-peakpowermanager-$(date +%Y%m%d-%H%M%S).txt}"

PID="$(pgrep -x peakpowermanager 2>/dev/null | head -n 1 || true)"

{
    echo "=== date ==="
    date
    echo

    echo "=== peakpowermanager process ==="
    if [ -z "$PID" ]; then
        echo "peakpowermanager not running"
        exit 0
    fi

    ps -p "$PID" -o pid=,ppid=,uid=,user=,comm=,command= 2>&1 || true
    echo

    echo "=== executable path ==="
    EXE="$(sudo lsof -a -p "$PID" -d txt -Fn 2>/dev/null |
        sed -n 's/^n//p' |
        head -n 1)"

    if [ -z "$EXE" ]; then
        echo "could not resolve executable with lsof"
        exit 0
    fi

    echo "$EXE"
    echo

    echo "=== code-signing identity ==="
    codesign -dvvv "$EXE" 2>&1 || true
    echo

    echo "=== entitlements ==="
    codesign -d --entitlements :- "$EXE" 2>&1 || true
    echo

    echo "=== entitlement highlights ==="
    codesign -d --entitlements :- "$EXE" 2>&1 |
        grep -i -E -A3 -B2         'ApplePPMUserClient|ppm|cpms|iokit-user-client|platform-application' ||
        true
    echo

    echo "=== imported IOKit symbols ==="
    nm -u "$EXE" 2>&1 |
        grep -E 'IOService(Open|GetMatchingService|Matching)|IOConnectCall' ||
        true
    echo

    echo "=== static IOServiceOpen callsites (20 instructions before each) ==="
    otool -tvV "$EXE" 2>&1 |
        awk '
            {
                ring[NR % 24] = $0
            }
            /bl[[:space:]]+_IOServiceOpen/ {
                print "--- callsite ---"
                start = NR - 20
                if (start < 1) start = 1
                for (i = start; i <= NR; i++) {
                    idx = i % 24
                    if (idx in ring)
                        print ring[idx]
                }
                print
            }
        ' || true
    echo

    echo "=== strings around PPM/CPMS ==="
    strings -a "$EXE" 2>&1 |
        grep -i -E 'ApplePassthroughPPM|ApplePPMUserClient|CPMS|PowerBudget|client.?id' |
        head -n 300 ||
        true
    echo

    echo "=== live ApplePPMUserClient ==="
    ioreg -r -c ApplePPMUserClient -l 2>&1 || true
    echo
} | tee "$OUT"

echo "wrote $OUT"
