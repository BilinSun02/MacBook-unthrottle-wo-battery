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
ECPU0  0x210e20020
PCPU0  0x211e20020
PCPU1  0x212e20020
```

The macOS prototype maps the corresponding 4 KiB page at
`cluster_base + 0x20000` on demand. Register reads use a read-only mapping;
the explicit `set-pstate` path maps it writable only for the selected
read-modify-write operation.

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

## Experimental results on the faulted-battery T6020 machine

The following behavior has been reproduced on the target M2 Pro MacBook Pro.

### Power-gated P-cluster MMIO can panic

Reading `CLUSTER_PSTATE` while a P cluster is power-gated causes an LLC
"Unavailable" bus-error panic. Two observed examples were:

```text
PCPU0  0x211e20020  faulted while PACC1 / cores 4-6 were offline
PCPU1  0x212e20020  faulted while PACC2 / cores 7-9 were offline
```

Under sufficient CPU load, `powermetrics` reports both P0 and P1 as 100%
online with 0% down residency, and the same command-register reads succeed.
Therefore these addresses are valid but their accessibility depends on the
corresponding cluster power domain being online.

### Battery throttle is visible in DESIRED1

With the machine otherwise idle, ECPU0 was observed at:

```text
CLUSTER_PSTATE = 0x0000000000400102
DESIRED1       = 2
```

and `powermetrics` showed almost all active E-cluster residency at 912 MHz.

A one-shot RMW to P-state 3 was accepted by hardware:

```text
before     0x0000000000400102
submitted  0x0000000002400103
after      0x0000000000400103
```

but macOS/APSC restored P-state 2 in under 100 ms.

Repeatedly reasserting the selected state every 10 ms successfully holds the
hardware near the requested frequency:

```text
ECPU0 pstate 3 -> ~1284 MHz
ECPU0 pstate 5 -> ~2004 MHz
ECPU0 pstate 7 -> ~2424 MHz
```

This demonstrates that `DESIRED1` is an effective control point for the
observed E-cluster throttle rather than merely a request ignored by a
downstream limiter.

### P-cluster command state under full load

With enough CPU burners to keep both P clusters continuously online, both
command registers were readable and returned the same value:

```text
PCPU0  0x0000000000404105
PCPU1  0x0000000000404105
```

Thus `DESIRED1 = 5` on both clusters at the sampled instant. During the same
sustained-load condition, `powermetrics` showed both clusters spending their
active time almost entirely at 1704 and 1968 MHz. The T6020 m1n1 default for
both P clusters is P-state 6, consistent with the observed state-5/state-6
operating range.

### Cross-cluster response when overriding one P cluster

With both P clusters held 100% online and 100% hardware-active by CPU burners,
holding only PCPU0 at P-state 7 produced a strongly asymmetric result:

```text
PCPU0 -> 2208 MHz (P-state 7), 100% residency
PCPU1 -> 1188 MHz (P-state 3), 100% residency
```

PCPU1 remained fully online and fully active, so the drop to 1188 MHz was not
caused by cluster power-gating or lack of work. This suggests a package-level
or cross-cluster policy that redistributes the fault/battery throttle when one
P cluster is forced above the policy-selected range. A direct PCPU1 command
read while PCPU0 is being held should distinguish policy-requested P-state 3
from a downstream hardware limit.

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

## ApplePassthroughPPM / CPMS capture on the faulted-battery machine

The macOS 26.6 / Darwin 25.6 T6020 system has
`com.apple.driver.ApplePassthroughPPM` loaded and an active
`ApplePassthroughPPM` service under the `ppm,passthrough` PMGR nub.

The service reports:

```text
CPMSSupported = 1
BaselineSystemCapability = (50000)
SystemCapabilityFallbackPowersLow = (3500,3500,3500)
EnableBatteryModelToSafeHarborFallback = 65536
ForceBatteryModelFallback = 0
UseOverrideClientPowerBudgets = 0
UseOverrideSystemCapability = 0
```

It also exposes paired value/enable properties for battery-model inputs,
including voltage, Qmax, depth-of-discharge, resistance, cutoff voltages,
temperature, measured battery power and related inputs, for example:

```text
OverrideBatteryInputV
UseOverrideBatteryInputV
OverrideBatteryInputQmax
UseOverrideBatteryInputQmax
OverrideBatteryInputPsCutoffVoltage
UseOverrideBatteryInputPsCutoffVoltage
OverrideBatteryInputPuCutoffVoltage
UseOverrideBatteryInputPuCutoffVoltage
```

This makes CPMS/PPM a substantially stronger candidate for the source of the
faulted-battery package throttle than independently patching CPU DVFS.

The PMGR `ppm` nub also contains:

```text
cpms-policy-type = 3
cpms-batt2client = <binary mapping containing "package">
cpms-dt-topology = <binary topology containing "droop" and "pulse_power.s">
cpms-dt-curve = <binary package curves>
btm-enabled = 1
```

The live IORegistry snapshot taken at idle reported:

```text
PPMVector:
  BaselineSysCap  = 50000
  ModeledSysCap   = 50000
  ProactiveSysCap = 50000
  NetSysCap       = 50000
```

Therefore the top-level system-capability value alone did not expose the
observed throttle in that idle snapshot. The live CPMS control state and
per-client budgets need to be queried directly.

### CLPC package controls

The T6020 AppleCLPC service has explicit package/client power controls,
including:

```text
~pkg-avg-max-power = 9961472
~pkg-lowpeak-max-power = 9961472
~pkg-power-split-cpu-fraction = 29491
~pkg-power-split-gpu-fraction = 29491
~pkg-power-split-ane-fraction = 6553
#pkg-avg-batt-power-target-tc = 1000
#pkg-avg-therm-power-target-tc = 250
```

This confirms that the same package-level stack has CPU/GPU/ANE power-split
concepts, which is why fixing the policy source is preferable to maintaining
per-engine frequency overrides.

### Read-only PPM probes

The CLI includes two userspace-only probes that do not use MBUnthrottle.kext:

```sh
./build/mbu ppm-cpms
./build/mbu ppm-client <client-id>
```

They use the reverse-engineered ApplePPMUserClient getters:

```text
selector 0x1e: CPMS control state, output buffer 0x28c0 bytes
selector 0x1d: client state, one scalar client ID, output buffer 0x640 bytes
```

The client-state command also decodes the known detailed-budget array at
offsets 0x1b8/0x1c0 and prints a compact non-zero hex dump for further
reverse engineering.

## Licensing

The Linux driver is GPL-2.0-only. This repository is GPL-2.0-only and keeps
the source provenance explicit.
