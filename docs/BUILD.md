# Build and loading notes

## Requirements

- M2 Pro / T6020 MacBook Pro
- Xcode with the macOS SDK installed
- administrator access
- a current backup

The code has not yet been compiled or run on the target machine, so treat the
first build as a portability pass.

## Build userspace first

```sh
make cli
```

This should produce:

```text
build/mbu
```

Running it before the kext is available should fail cleanly with
`MBUnthrottleService not found`.

## Build the kext

```sh
make kext
```

The plain-Make target uses the public Kernel.framework headers and produces:

```text
build/MBUnthrottle.kext
```

Modern Xcode SDKs have changed C++ ownership annotations and kext build details
over time. A compile failure here is expected to be actionable rather than
proof that the approach is impossible. Please capture the complete compiler
error instead of changing types blindly.

## Apple-silicon kext policy

On Apple silicon, third-party kernel extensions require Reduced Security and
the option allowing user management of kernel extensions. They are incorporated
into the Auxiliary Kernel Collection and require user approval plus a restart;
they are not simply loaded on demand like older Intel-era development kexts.

Apple's current security documentation:

https://support.apple.com/en-ie/guide/security-pdf/sec8e454101b/web

Do not disable unrelated security controls just to get past a build or signing
problem.

A locally built experimental kext may also require an appropriate signing
identity before macOS will accept it. The exact installation/signing workflow
depends on the macOS release and developer-account setup, so this repository
does not currently ship a one-size-fits-all installer.

## Verify the machine before loading

User space:

```sh
ioreg -p IODeviceTree -d 1 -l | grep -i t6020
system_profiler SPHardwareDataType
```

The kext itself also checks the root device-tree `compatible` property for
`apple,t6020` and refuses to start otherwise.

## First diagnostic run

Once the service is present:

```sh
sudo ./build/mbu status
```

Capture status once at idle and once while the CPU is under sustained load.

Do not infer an "actual P-state" from `raw_status` yet. The T6020 status
layout is intentionally treated as unknown in this prototype.

## Useful accompanying data

```sh
pmset -g batt
system_profiler SPPowerDataType
sysctl -n machdep.cpu.brand_string 2>/dev/null || true
```

If available on the installed macOS release, a short `powermetrics` sample
during load is also useful for correlating requested state with observed CPU
frequency.

## Recovery plan

Because this is a kext experiment, know how to return to recoveryOS before
testing. If the extension prevents normal boot, use the standard Apple recovery
path to undo the experimental kext/AuxKC change rather than repeatedly forcing
boots.
