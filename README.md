# MacBook unthrottle without battery

Experimental macOS research prototype for M2 Pro (T6020) MacBook Pro systems that become severely CPU-throttled when the battery pack is deeply discharged or faulted.

This project ports the **CPU DVFS register primitives** reverse-engineered by the Asahi Linux project back to macOS. It is deliberately diagnostic-first: read the raw DVFS state, then optionally request a P-state through the same cluster control register used by m1n1/Linux.

## Status

**Very early prototype. Not tested on hardware yet.**

Known T6020 cluster bases from Asahi m1n1:

| Cluster | Base | m1n1 default P-state |
|---|---:|---:|
| ECPU0 | `0x210e00000` | 5 |
| PCPU0 | `0x211e00000` | 6 |
| PCPU1 | `0x212e00000` | 6 |

For T6020, m1n1 accesses the P-state command register at cluster base + `0x20020`. The Linux Apple SoC cpufreq driver defines the corresponding DVFS block offsets:

- command: `+0x20`
- last-change counter: `+0x38`
- status: `+0x50`
- PLL status: `+0xc0`
- PLL factor: `+0xc8`

This prototype maps the page beginning at cluster base + `0x20000`.

### Important T6020 detail

Linux currently treats newer SoCs with a fallback status layout. Therefore this project does **not** pretend that the raw `DVFS_STATUS` field is a decoded "actual P-state" on T6020. It reports:

- raw command
- requested P-state decoded from command bits [4:0]
- raw status
- last-change counter
- raw PLL status/factor

That gives us enough information to distinguish "macOS keeps requesting a low P-state" from "our requested state is being overwritten/rejected" without inventing a T6020 status bit layout.

## What it can do

The proposed kext/user-client interface supports:

```
mbu status
mbu set ECPU0 <0..15>
mbu set PCPU0 <0..15>
mbu set PCPU1 <0..15>
mbu restore-default
mbu hold-default [interval-ms]
```

`restore-default` requests the conservative m1n1 boot defaults (5/6/6), **not an assumed maximum/boost state**.

`hold-default` repeatedly reasserts those values from userspace. This is useful if Apple's own power-management driver immediately rewrites the command register.

## Safety / scope

This writes undocumented SoC MMIO from kernel context. A bad physical address or register value can panic the machine, corrupt state, overheat hardware, or cause data loss.

The driver:
- refuses non-T6020 systems unless its compatibility check succeeds;
- accepts only 4-bit P-state values for the first prototype;
- preserves all unrelated command-register bits;
- polls the hardware busy bit before issuing a request;
- does **not** modify voltage tables, PMGR, PMP, SMC, thermal limits, battery firmware, or Asahi's `ppt-thrtl` / `llc-thrtl` / `amx-thrtl` feature registers.

Those throttle-feature registers are documented in `docs/research.md` but intentionally left read-only/out of scope until the simpler DVFS experiment tells us which layer is enforcing the battery-related cap.

## Build

This needs to be built **on macOS with Xcode installed**. The userspace CLI can be built with:

```sh
make cli
```

The kext build target is intentionally marked experimental because modern macOS/Xcode kext toolchains vary. See `docs/BUILD.md`.

Loading a third-party kext on Apple silicon requires the appropriate Reduced Security / kernel-extension settings and user approval. Do not disable more platform security than needed.

## First test

After loading the kext:

```sh
sudo ./build/mbu status
sudo ./build/mbu restore-default
sudo ./build/mbu status
```

Then put the CPU under load and watch:

```sh
sudo ./build/mbu hold-default 50
```

Press Ctrl-C to stop reasserting the state.

The most useful first report is the before/after output of `mbu status` plus observed CPU frequency/performance.

## Reverse-engineering sources

This project is based on behavior documented in:

- Asahi Linux / m1n1: `src/cpufreq.c`
- Linux: `drivers/cpufreq/apple-soc-cpufreq.c`

See `docs/research.md` for exact constants and attribution.

## License

GPL-2.0-only. The project intentionally uses a GPL-compatible license because the Linux Apple cpufreq driver is an important reference implementation.
