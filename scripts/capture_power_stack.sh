#!/bin/sh
set -u

OUT="${1:-mbu-power-stack-$(date +%Y%m%d-%H%M%S).txt}"

run()
{
    echo "=== $1 ==="
    shift
    "$@" 2>&1 || true
    echo
}

{
    echo "=== date ==="
    date
    echo

    echo "=== uname ==="
    uname -a
    echo

    echo "=== loaded power-related kexts ==="
    kmutil showloaded 2>&1 |
        grep -i -E 'ApplePMGR|AppleCLPC|PassthroughPPM|AppleSMC|AGX|GPU|Power' ||
        true
    echo

    echo "=== IOService: AppleCLPC ==="
    ioreg -lw0 -c AppleCLPC 2>&1 || true
    echo

    echo "=== IOService: ApplePassthroughPPM ==="
    ioreg -lw0 -c ApplePassthroughPPM 2>&1 || true
    echo

    echo "=== IODeviceTree: clpc ==="
    ioreg -p IODeviceTree -lw0 -n clpc 2>&1 || true
    echo

    echo "=== IODeviceTree: ppm ==="
    ioreg -p IODeviceTree -lw0 -n ppm 2>&1 || true
    echo

    echo "=== IODeviceTree: pmgr filtered power properties ==="
    ioreg -p IODeviceTree -lw0 -n pmgr 2>&1 |
        grep -i -E         'cpms|pmax|cutoff|batt|battery|pkg-|gpu-|cpu-|power|thermal|clpc|ppm|ppt' ||
        true
    echo

    echo "=== whole IORegistry filtered power-management names ==="
    ioreg -lw0 2>&1 |
        grep -i -E         'AppleCLPC|ApplePassthroughPPM|ApplePMGR|cpms|pmax|cutoff|batt2client|battery-data-input-source|pkg-.*power|gpu-.*power|cpu-.*power' ||
        true
    echo

    echo "=== MBUnthrottle metadata ==="
    if [ -x ./build/mbu ]; then
        sudo ./build/mbu status ECPU0 PCPU0 PCPU1 2>&1 || true
    else
        echo "./build/mbu not found; run make cli first"
    fi
    echo
} | tee "$OUT"

echo "wrote $OUT"
