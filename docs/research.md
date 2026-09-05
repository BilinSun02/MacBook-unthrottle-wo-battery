# T6020 DVFS research notes

This document records the provenance of the constants used by the macOS diagnostic prototype.

## Asahi m1n1

Primary source:

https://github.com/AsahiLinux/m1n1/blob/main/src/cpufreq.c

m1n1 defines:

```text
CLUSTER_PSTATE = 0x20020
CLUSTER_PSTATE_BUSY = bit 31
CLUSTER_PSTATE_SET = bit 25
CLUSTER_PSTATE_DESIRED1 = bits 4:0
```

For T6020 it lists:

```text
ECPU0  base 0x210e00000  APSC pstate 1  default pstate 5
PCPU0  base 0x211e00000  APSC pstate 1  default pstate 6
PCPU1  base 0x212e00000  APSC pstate 1  default pstate 6
```

Therefore the command register physical addresses are:

```text
ECPU0  0x211000020
PCPU0  0x212000020
PCPU1  0x213000020
```

The current macOS prototype maps the corresponding 4 KiB page at
`cluster_base + 0x20000` read-only.

## Linux apple-soc-cpufreq

Primary source:

https://github.com/torvalds/linux/blob/master/drivers/cpufreq/apple-soc-cpufreq.c

Relevant offsets:

```text
APPLE_DVFS_CMD             0x20
APPLE_DVFS_LAST_CHG_TIME   0x38
APPLE_DVFS_STATUS          0x50
APPLE_DVFS_PLL_STATUS      0xc0
APPLE_DVFS_PLL_FACTOR      0xc8
```

The command field used by newer SoCs is bits 4:0. Linux sets bit 25 when
requesting a performance-state change and waits for bit 31 (busy) to clear.

### Why status remains raw

The Linux driver has SoC-specific `DVFS_STATUS` layouts for older chips but
uses a fallback for newer SoCs where the layout is not known. In that fallback
it derives the reported P-state from the command register instead of claiming
that `DVFS_STATUS` has a known current-state field.

The macOS prototype follows the same conservative rule for T6020:
`raw_status` is exposed without decoding it into an "actual P-state".

## Other T6020 controls observed in m1n1

m1n1's T6020 feature table contains:

```text
cpu-apsc                  CLUSTER_PSTATE bit 23 semantics
ppt-thrtl                 cluster + 0x48400, bit 63
llc-thrtl                 cluster + 0x40270, bit 63
amx-thrtl                 cluster + 0x40250, bit 63
cpu-fixed-freq-pll-relock CLUSTER_PSTATE bit 42
```

These names are reverse-engineering labels, not necessarily a complete
description of the hardware semantics.

The current project deliberately does not modify or expose these registers.
They are the next things to investigate only if the read-only capture shows
that the normal DVFS command field is not the layer producing the battery
fault throttle.

## Questions the first capture should answer

1. Under sustained CPU load with the faulted battery, what requested P-state
   appears in command bits 4:0 for each cluster?
2. Does the requested state change between idle and load?
3. Does `last_change` advance while the machine is throttled?
4. Do PLL status/factor values change when the requested state changes?
5. Does Apple's power-management stack rewrite the command rapidly enough that
   repeated samples show multiple requested states?

The answers determine whether the next experiment belongs at the DVFS-command
layer or farther downstream in PMGR/PMP/SMC/throttle controls.

## Licensing

The Linux driver is GPL-2.0-only. This repository is GPL-2.0-only and keeps
the source provenance explicit.
