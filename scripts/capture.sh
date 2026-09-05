#!/bin/sh
set -u

OUT="${1:-mbu-capture-$(date +%Y%m%d-%H%M%S).txt}"

{
  echo "=== date ==="
  date
  echo

  echo "=== uname ==="
  uname -a
  echo

  echo "=== hardware ==="
  system_profiler SPHardwareDataType 2>&1
  echo

  echo "=== battery ==="
  pmset -g batt 2>&1
  echo
  system_profiler SPPowerDataType 2>&1
  echo

  echo "=== T6020 device-tree check ==="
  ioreg -p IODeviceTree -d 1 -l 2>&1 | grep -i -E 'compatible|t6020' || true
  echo

  echo "=== MBUnthrottle status ==="
  if [ -x ./build/mbu ]; then
    sudo ./build/mbu status 2>&1
  else
    echo "./build/mbu not found; run make cli first"
  fi
} | tee "$OUT"

echo "wrote $OUT"
