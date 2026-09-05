// SPDX-License-Identifier: GPL-2.0-only

#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>

#include <mach/mach_error.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include "../include/MBUProtocol.h"

static const char *
clusterName(unsigned i)
{
    switch (i) {

        case kMBUClusterECPU0:
            return "ECPU0";

        case kMBUClusterPCPU0:
            return "PCPU0";

        case kMBUClusterPCPU1:
            return "PCPU1";

        default:
            return "?";
    }
}

static int
clusterIndex(const char *name)
{
    if (!name)
        return -1;

    if (std::strcmp(name, "ECPU0") == 0)
        return kMBUClusterECPU0;

    if (std::strcmp(name, "PCPU0") == 0)
        return kMBUClusterPCPU0;

    if (std::strcmp(name, "PCPU1") == 0)
        return kMBUClusterPCPU1;

    return -1;
}

static bool
parseStatusMask(int argc,
                char **argv,
                uint32_t *mask)
{
    if (!mask || argc < 3)
        return false;

    *mask = 0;

    for (int argi = 2;
         argi < argc;
         ++argi) {

        const int cluster =
            clusterIndex(argv[argi]);

        if (cluster < 0) {
            std::fprintf(
                stderr,
                "unknown cluster: %s\n",
                argv[argi]);
            return false;
        }

        if (cluster == kMBUClusterPCPU1) {
            std::fprintf(
                stderr,
                "warning: PCPU1 previously caused an LLC Bus Error "
                "at 0x212e20020 while unavailable; proceeding because "
                "you explicitly selected it.\n");
        }

        *mask |=
            1U << static_cast<unsigned>(
                cluster);
    }

    return *mask != 0;
}

static const char *
registerName(unsigned r)
{
    switch (r) {
        case kMBURegisterCmd:
            return "cmd";
        case kMBURegisterLastChange:
            return "last_change";
        case kMBURegisterStatus:
            return "status";
        case kMBURegisterPLLStatus:
            return "pll_status";
        case kMBURegisterPLLFactor:
            return "pll_factor";
        default:
            return "?";
    }
}

static int
registerIndex(const char *name)
{
    if (!name)
        return -1;

    if (std::strcmp(name, "cmd") == 0)
        return kMBURegisterCmd;
    if (std::strcmp(name, "last_change") == 0)
        return kMBURegisterLastChange;
    if (std::strcmp(name, "status") == 0)
        return kMBURegisterStatus;
    if (std::strcmp(name, "pll_status") == 0)
        return kMBURegisterPLLStatus;
    if (std::strcmp(name, "pll_factor") == 0)
        return kMBURegisterPLLFactor;

    return -1;
}

static io_connect_t
openConnection()
{
    io_service_t service =
        IOServiceGetMatchingService(
            kIOMainPortDefault,
            IOServiceMatching(
                "MBUnthrottleService"));

    if (!service) {

        std::fprintf(
            stderr,
            "MBUnthrottleService not found\n");

        return IO_OBJECT_NULL;
    }

    CFTypeRef property =
        IORegistryEntryCreateCFProperty(
            service,
            CFSTR("MBUProtocolVersion"),
            kCFAllocatorDefault,
            0);

    if (!property ||
        CFGetTypeID(property) !=
            CFNumberGetTypeID()) {

        if (property)
            CFRelease(property);

        std::fprintf(
            stderr,
            "loaded MBUnthrottleService does not advertise "
            "MBUProtocolVersion; the running kext is stale/older "
            "than this CLI. Re-stage the current kext, run kmutil "
            "load before reboot, reboot, then kmutil load again.\n");

        IOObjectRelease(service);

        return IO_OBJECT_NULL;
    }

    int32_t kernelProtocol = 0;

    const Boolean gotProtocol =
        CFNumberGetValue(
            static_cast<CFNumberRef>(property),
            kCFNumberSInt32Type,
            &kernelProtocol);

    CFRelease(property);

    if (!gotProtocol ||
        kernelProtocol !=
            static_cast<int32_t>(
                kMBUProtocolVersion)) {

        std::fprintf(
            stderr,
            "protocol mismatch before IOServiceOpen: "
            "kernel=%d user=%u\n",
            kernelProtocol,
            kMBUProtocolVersion);

        IOObjectRelease(service);

        return IO_OBJECT_NULL;
    }

    io_connect_t connect =
        IO_OBJECT_NULL;

    kern_return_t kr =
        IOServiceOpen(
            service,
            mach_task_self(),
            0,
            &connect);

    IOObjectRelease(service);

    if (kr != KERN_SUCCESS) {

        std::fprintf(
            stderr,
            "IOServiceOpen: %s "
            "(0x%x)\n",
            mach_error_string(kr),
            kr);

        return IO_OBJECT_NULL;
    }

    return connect;
}

static int
printStatus(io_connect_t connect,
            uint32_t clusterMask)
{
    MBUStatusRequest request{};
    request.cluster_mask =
        clusterMask;

    MBUStatusReply reply{};

    size_t replySize =
        sizeof(reply);

    kern_return_t kr =
        IOConnectCallStructMethod(
            connect,
            kMBUSelectorGetStatus,
            &request,
            sizeof(request),
            &reply,
            &replySize);

    if (kr != KERN_SUCCESS) {

        std::fprintf(
            stderr,
            "status failed: %s "
            "(0x%x)\n",
            mach_error_string(kr),
            kr);

        return 1;
    }

    if (reply.protocol_version
        != kMBUProtocolVersion) {

        std::fprintf(
            stderr,
            "protocol mismatch: "
            "kernel=%u user=%u\n",
            reply.protocol_version,
            kMBUProtocolVersion);

        return 1;
    }

    std::printf(
        "protocol=%u clusters=%u flags=0x%x\n",
        reply.protocol_version,
        reply.cluster_count,
        reply.flags);

    std::printf(
        "metadata-only status; no MMIO was accessed\n");

    for (unsigned i = 0;
         i < reply.cluster_count &&
         i < kMBUClusterCount;
         ++i) {

        const auto &s =
            reply.clusters[i];

        if ((s.flags &
             kMBUClusterFlagSelected) == 0)
            continue;

        std::printf(
            "%s\n"
            "  base          0x%llx\n"
            "  cmd_phys      0x%llx\n"
            "  m1n1_default  %u\n",
            clusterName(i),
            static_cast<
                unsigned long long>(
                    s.cluster_base),
            static_cast<
                unsigned long long>(
                    s.command_phys),
            s.default_pstate);
    }

    return 0;
}

static int
readRegister(io_connect_t connect,
             unsigned cluster,
             unsigned reg)
{
    MBUReadRequest request{};
    request.cluster =
        static_cast<uint32_t>(cluster);
    request.reg =
        static_cast<uint32_t>(reg);

    MBUReadReply reply{};
    size_t replySize =
        sizeof(reply);

    kern_return_t kr =
        IOConnectCallStructMethod(
            connect,
            kMBUSelectorReadRegister,
            &request,
            sizeof(request),
            &reply,
            &replySize);

    if (kr != KERN_SUCCESS) {
        std::fprintf(
            stderr,
            "read failed: %s (0x%x)\n",
            mach_error_string(kr),
            kr);
        return 1;
    }

    if (reply.protocol_version
        != kMBUProtocolVersion) {

        std::fprintf(
            stderr,
            "protocol mismatch: kernel=%u user=%u\n",
            reply.protocol_version,
            kMBUProtocolVersion);
        return 1;
    }

    std::printf(
        "%s %s\n"
        "  phys   0x%llx\n"
        "  value  0x%016llx\n",
        clusterName(reply.cluster),
        registerName(reply.reg),
        static_cast<unsigned long long>(
            reply.physical_address),
        static_cast<unsigned long long>(
            reply.value));

    return 0;
}

static int
setPState(io_connect_t connect,
          unsigned cluster,
          unsigned pstate)
{
    MBUSetPStateRequest request{};
    request.cluster =
        static_cast<uint32_t>(cluster);
    request.pstate =
        static_cast<uint32_t>(pstate);

    MBUSetPStateReply reply{};
    size_t replySize =
        sizeof(reply);

    kern_return_t kr =
        IOConnectCallStructMethod(
            connect,
            kMBUSelectorSetPState,
            &request,
            sizeof(request),
            &reply,
            &replySize);

    if (kr != KERN_SUCCESS) {
        std::fprintf(
            stderr,
            "set-pstate failed: %s (0x%x)\n",
            mach_error_string(kr),
            kr);
        return 1;
    }

    if (reply.protocol_version
        != kMBUProtocolVersion) {

        std::fprintf(
            stderr,
            "protocol mismatch: kernel=%u user=%u\n",
            reply.protocol_version,
            kMBUProtocolVersion);
        return 1;
    }

    std::printf(
        "%s pstate %u\n"
        "  phys       0x%llx\n"
        "  before     0x%016llx\n"
        "  submitted  0x%016llx\n"
        "  after      0x%016llx\n",
        clusterName(reply.cluster),
        reply.requested_pstate,
        static_cast<unsigned long long>(
            reply.physical_address),
        static_cast<unsigned long long>(
            reply.command_before),
        static_cast<unsigned long long>(
            reply.command_submitted),
        static_cast<unsigned long long>(
            reply.command_after));

    return 0;
}

static int
restoreDefaults(io_connect_t connect)
{
    kern_return_t kr =
        IOConnectCallStructMethod(
            connect,
            kMBUSelectorRestoreDefaults,
            nullptr,
            0,
            nullptr,
            nullptr);

    if (kr != KERN_SUCCESS) {

        std::fprintf(
            stderr,
            "restore-default failed: %s "
            "(0x%x)\n",
            mach_error_string(kr),
            kr);

        return 1;
    }

    return 0;
}

static void
usage(const char *argv0)
{
    std::fprintf(
        stderr,
        "usage:\n"
        "  %s status <ECPU0|PCPU0|PCPU1> [cluster ...]\n"
        "  %s read <ECPU0|PCPU0|PCPU1> "
        "<cmd|last_change|status|pll_status|pll_factor>\n"
        "  %s set-pstate <ECPU0|PCPU0|PCPU1> <pstate>\n"
        "  %s restore-default\n"
        "  %s hold-default [milliseconds]\n"
        "\n"
        "status is metadata-only. read performs exactly one "
        "64-bit MMIO load. set-pstate performs one explicit "
        "read-modify-write transition on the selected cluster.\n",
        argv0,
        argv0,
        argv0,
        argv0,
        argv0);
}

int
main(int argc, char **argv)
{
    if (argc < 2) {
        usage(argv[0]);
        return 2;
    }

    io_connect_t connect =
        openConnection();

    if (connect == IO_OBJECT_NULL)
        return 1;

    int result = 0;

    if (std::strcmp(
            argv[1],
            "status") == 0) {

        uint32_t clusterMask = 0;

        if (!parseStatusMask(
                argc,
                argv,
                &clusterMask)) {

            usage(argv[0]);
            result = 2;
        } else {
            result =
                printStatus(
                    connect,
                    clusterMask);
        }
    }

    else if (std::strcmp(
                 argv[1],
                 "read") == 0) {

        if (argc != 4) {
            usage(argv[0]);
            result = 2;
        } else {
            const int cluster =
                clusterIndex(argv[2]);

            const int reg =
                registerIndex(argv[3]);

            if (cluster < 0 || reg < 0) {
                usage(argv[0]);
                result = 2;
            } else {
                if (cluster ==
                    kMBUClusterPCPU1) {
                    std::fprintf(
                        stderr,
                        "warning: PCPU1 cmd previously caused an LLC "
                        "Bus Error while unavailable; this command will "
                        "perform the requested access exactly.\n");
                }

                result =
                    readRegister(
                        connect,
                        static_cast<unsigned>(
                            cluster),
                        static_cast<unsigned>(
                            reg));
            }
        }
    }

    else if (std::strcmp(
                 argv[1],
                 "set-pstate") == 0) {

        if (argc != 4) {
            usage(argv[0]);
            result = 2;
        } else {
            const int cluster =
                clusterIndex(argv[2]);

            char *end = nullptr;
            const long parsed =
                std::strtol(
                    argv[3],
                    &end,
                    10);

            if (cluster < 0 ||
                !end ||
                *end != '\0' ||
                parsed < 1 ||
                parsed > 31) {

                usage(argv[0]);
                result = 2;
            } else {
                const unsigned maxPState =
                    cluster ==
                        kMBUClusterECPU0
                        ? 7U
                        : 17U;

                if (static_cast<unsigned>(
                        parsed) > maxPState) {

                    std::fprintf(
                        stderr,
                        "pstate out of known T6020 range: "
                        "%s accepts 1..%u\n",
                        clusterName(
                            static_cast<unsigned>(
                                cluster)),
                        maxPState);

                    result = 2;
                } else {
                    if (cluster !=
                        kMBUClusterECPU0) {

                        std::fprintf(
                            stderr,
                            "warning: P-cluster MMIO can panic if "
                            "that cluster is power-gated; proceeding "
                            "because you explicitly selected it.\n");
                    }

                    result =
                        setPState(
                            connect,
                            static_cast<unsigned>(
                                cluster),
                            static_cast<unsigned>(
                                parsed));
                }
            }
        }
    }

    else if (std::strcmp(
                 argv[1],
                 "restore-default") == 0) {

        result =
            restoreDefaults(connect);

        if (result == 0)
            result =
                printStatus(
                    connect,
                    kMBUClusterMaskECPU0 |
                    kMBUClusterMaskPCPU0);
        else
            std::fprintf(
                stderr,
                "MMIO writes are intentionally disabled in this "
                "diagnostic build.\n");
    }

    else if (std::strcmp(
                 argv[1],
                 "hold-default") == 0) {

        unsigned intervalMs = 50;

        if (argc >= 3) {

            const long parsed =
                std::strtol(
                    argv[2],
                    nullptr,
                    10);

            /*
             * Deliberately don't permit an
             * aggressive sub-10-ms userspace
             * register fight.
             */
            if (parsed < 10 ||
                parsed > 5000) {

                std::fprintf(
                    stderr,
                    "interval must be "
                    "10..5000 ms\n");

                IOServiceClose(connect);

                return 2;
            }

            intervalMs =
                static_cast<unsigned>(
                    parsed);
        }

        std::printf(
            "reasserting m1n1 T6020 "
            "default states every %u ms\n"
            "Ctrl-C to stop\n",
            intervalMs);

        for (;;) {

            if (restoreDefaults(connect)
                != 0) {

                result = 1;
                break;
            }

            usleep(
                intervalMs * 1000);
        }
    }

    else {

        usage(argv[0]);
        result = 2;
    }

    IOServiceClose(connect);

    return result;
}
