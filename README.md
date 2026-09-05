# MacBook unthrottle without battery

Experimental macOS research project for M2 Pro (T6020) MacBook Pro systems that become severely CPU-throttled when the battery pack is deeply discharged or faulted.

The immediate goal is to port enough of Asahi Linux's Apple-Silicon DVFS reverse engineering back to macOS to determine **where the battery-related throttle is enforced**.

## Current status

**Read-only diagnostic prototype. Not yet tested on hardware.**

The current kext maps the three known T6020 CPU-cluster DVFS pages read-only and exposes their raw state to a small root-only userspace CLI.

Known T6020 cluster bases from Asahi m1n1:

| Cluster | Base | m1n1 default P-state |
|---|---:|---:|
| ECPU0 | `0x210e00000` | 5 |
| PCPU0 | `0x211e00000` | 6 |
| PCPU1 | `0x212e00000` | 6 |

m1n1 accesses the cluster P-state register at cluster base + `0x20020`. Linux's Apple SoC cpufreq driver describes the same DVFS block with these offsets:

- command: `+0x20`
- last-change counter: `+0x38`
- status: `+0x50`
- PLL status: `+0xc0`
- PLL factor: `+0xc8`

The kext maps the page beginning at cluster base + `0x20000`.

### Important T6020 detail

Linux currently uses a fallback for newer Apple SoCs where it does not assume a decoded `DVFS_STATUS` layout. This project follows that conservative approach.

`mbu status` reports:

- raw command register
- requested P-state decoded from command bits [4:0]
- raw status register
- last-change counter
- raw PLL status/factor

It deliberately does **not** label `DVFS_STATUS` as an "actual P-state" on T6020.

## Why this matters

If a battery-faulted Mac shows a persistently low requested P-state in the command register while under CPU load, that strongly suggests macOS / Apple's CPU power-management stack is actively asking for the low state.

If the command register requests a normal/high state while the observed performance remains capped, the enforcement is likely farther downstream: PMGR/PMP/SMC, another throttle feature, or an electrical power-budget limit.

Asahi m1n1 also documents T6020 feature controls named:

- `cpu-apsc`
- `ppt-thrtl`
- `llc-thrtl`
- `amx-thrtl`
- `cpu-fixed-freq-pll-relock`

Those are recorded in `docs/research.md`, but this first prototype does not modify them.

## Build

Build on the affected Mac with Xcode installed.

Userspace CLI:

```sh
make cli
```

Experimental kext build:

```sh
make kext
```

See `docs/BUILD.md` before attempting to load it.

## First capture

After loading the kext:

```sh
sudo ./build/mbu status
```

Take one capture while idle and another while the CPU is under sustained load. Also note the observed clock/performance behavior.

That capture is the input needed for the next reverse-engineering step.

## Safety

This is experimental kernel code using undocumented SoC MMIO addresses. The current implementation maps those ranges read-only and refuses to start unless the device-tree root reports `apple,t6020`.

No voltage, PMGR, PMP, SMC, battery-controller, thermal-limit, or throttle-control register is modified by the current code.

## Sources

- Asahi Linux m1n1: `src/cpufreq.c`
- Linux: `drivers/cpufreq/apple-soc-cpufreq.c`
- Apple Kernel API: `IOMemoryDescriptor::withPhysicalAddress`

See `docs/research.md` for the exact provenance of constants.

## License

GPL-2.0-only.
