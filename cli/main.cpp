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
openServiceConnection(const char *serviceClass,
                      uint32_t type)
{
    io_service_t service =
        IOServiceGetMatchingService(
            kIOMainPortDefault,
            IOServiceMatching(serviceClass));

    if (!service) {
        std::fprintf(
            stderr,
            "%s not found\n",
            serviceClass);
        return IO_OBJECT_NULL;
    }

    io_connect_t connect =
        IO_OBJECT_NULL;

    kern_return_t kr =
        IOServiceOpen(
            service,
            mach_task_self(),
            type,
            &connect);

    IOObjectRelease(service);

    if (kr != KERN_SUCCESS) {
        std::fprintf(
            stderr,
            "IOServiceOpen(%s,type=%u): %s (0x%x)\n",
            serviceClass,
            type,
            mach_error_string(kr),
            kr);
        return IO_OBJECT_NULL;
    }

    return connect;
}

static void
dumpNonZeroHex(const unsigned char *data,
               size_t size)
{
    for (size_t offset = 0;
         offset < size;
         offset += 16) {

        const size_t count =
            size - offset < 16
                ? size - offset
                : 16;

        bool nonzero = false;

        for (size_t i = 0;
             i < count;
             ++i) {

            if (data[offset + i] != 0) {
                nonzero = true;
                break;
            }
        }

        if (!nonzero)
            continue;

        std::printf(
            "%04zx:",
            offset);

        for (size_t i = 0;
             i < count;
             ++i) {

            std::printf(
                " %02x",
                data[offset + i]);
        }

        std::printf("\n");
    }
}

static int
ppmCpms()
{
    static constexpr uint32_t kSelector =
        0x1e;

    static constexpr size_t kOutputBytes =
        0x28c0;

    io_connect_t connect =
        openServiceConnection(
            "ApplePassthroughPPM",
            0);

    if (connect == IO_OBJECT_NULL)
        return 1;

    unsigned char output[kOutputBytes]{};

    size_t outputSize =
        sizeof(output);

    kern_return_t kr =
        IOConnectCallMethod(
            connect,
            kSelector,
            nullptr,
            0,
            nullptr,
            0,
            nullptr,
            nullptr,
            output,
            &outputSize);

    IOServiceClose(connect);

    if (kr != KERN_SUCCESS) {
        std::fprintf(
            stderr,
            "ppm-cpms selector=0x%x failed: %s (0x%x)\n",
            kSelector,
            mach_error_string(kr),
            kr);
        return 1;
    }

    std::printf(
        "ApplePassthroughPPM CPMS control state\n"
        "  selector     0x%x\n"
        "  output_size  0x%zx (%zu)\n"
        "  nonzero 16-byte rows:\n",
        kSelector,
        outputSize,
        outputSize);

    dumpNonZeroHex(
        output,
        outputSize);

    return 0;
}

static int
ppmClient(unsigned client)
{
    static constexpr uint32_t kSelector =
        0x1d;

    static constexpr size_t kOutputBytes =
        0x640;

    static constexpr size_t kCountOffset =
        0x1b8;

    static constexpr size_t kEntryOffset =
        0x1c0;

    static constexpr uint32_t kEntryMax =
        8;

    io_connect_t connect =
        openServiceConnection(
            "ApplePassthroughPPM",
            0);

    if (connect == IO_OBJECT_NULL)
        return 1;

    uint64_t inputScalars[1] = {
        client,
    };

    unsigned char output[kOutputBytes]{};

    size_t outputSize =
        sizeof(output);

    kern_return_t kr =
        IOConnectCallMethod(
            connect,
            kSelector,
            inputScalars,
            1,
            nullptr,
            0,
            nullptr,
            nullptr,
            output,
            &outputSize);

    IOServiceClose(connect);

    if (kr != KERN_SUCCESS) {
        std::fprintf(
            stderr,
            "ppm-client %u selector=0x%x failed: %s (0x%x)\n",
            client,
            kSelector,
            mach_error_string(kr),
            kr);
        return 1;
    }

    std::printf(
        "ApplePassthroughPPM client %u\n"
        "  selector     0x%x\n"
        "  output_size  0x%zx (%zu)\n",
        client,
        kSelector,
        outputSize,
        outputSize);

    if (outputSize >=
        kCountOffset + sizeof(uint32_t)) {

        uint32_t detailedCount = 0;

        std::memcpy(
            &detailedCount,
            output + kCountOffset,
            sizeof(detailedCount));

        std::printf(
            "  detailed_budget_count  %u\n",
            detailedCount);

        const uint32_t count =
            detailedCount < kEntryMax
                ? detailedCount
                : kEntryMax;

        for (uint32_t i = 0;
             i < count;
             ++i) {

            const size_t offset =
                kEntryOffset
                + static_cast<size_t>(i)
                * 16;

            if (offset + 16 > outputSize)
                break;

            uint32_t budget = 0;
            uint64_t details = 0;

            std::memcpy(
                &budget,
                output + offset + 4,
                sizeof(budget));

            std::memcpy(
                &details,
                output + offset + 8,
                sizeof(details));

            std::printf(
                "  detailed[%u] client=%u budget=%u details=0x%016llx\n",
                i,
                output[offset],
                budget,
                static_cast<unsigned long long>(
                    details));
        }
    }

    std::printf(
        "  nonzero 16-byte rows:\n");

    dumpNonZeroHex(
        output,
        outputSize);

    return 0;
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
            "than this CLI. Re-stage/sign the current kext, approve it if "
            "prompted, reboot, then run kmutil load once after reboot.\n");

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
          unsigned pstate,
          bool verbose = true)
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

    if (verbose) {
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
    }

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
        "  %s hold-pstate <ECPU0|PCPU0|PCPU1> <pstate> [milliseconds]\n"
        "  %s ppm-cpms\n"
        "  %s ppm-client <client-id>\n"
        "  %s restore-default\n"
        "  %s hold-default [milliseconds]\n"
        "\n"
        "ppm-cpms and ppm-client are read-only ApplePassthroughPPM "
        "queries and do not require MBUnthrottle.kext.\n"
        "status is metadata-only. read performs exactly one "
        "64-bit MMIO load. set-pstate performs one explicit "
        "read-modify-write transition on the selected cluster.\n",
        argv0,
        argv0,
        argv0,
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

    if (std::strcmp(
            argv[1],
            "ppm-cpms") == 0) {

        if (argc != 2) {
            usage(argv[0]);
            return 2;
        }

        return ppmCpms();
    }

    if (std::strcmp(
            argv[1],
            "ppm-client") == 0) {

        if (argc != 3) {
            usage(argv[0]);
            return 2;
        }

        char *end = nullptr;

        const unsigned long parsed =
            std::strtoul(
                argv[2],
                &end,
                0);

        if (!end ||
            *end != '\0' ||
            parsed > 255) {

            std::fprintf(
                stderr,
                "client-id must be 0..255\n");

            return 2;
        }

        return ppmClient(
            static_cast<unsigned>(
                parsed));
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
                 "hold-pstate") == 0) {

        if (argc < 4 || argc > 5) {
            usage(argv[0]);
            result = 2;
        } else {
            const int cluster =
                clusterIndex(argv[2]);

            char *end = nullptr;
            const long parsedPState =
                std::strtol(
                    argv[3],
                    &end,
                    10);

            if (cluster < 0 ||
                !end ||
                *end != '\0' ||
                parsedPState < 1 ||
                parsedPState > 31) {

                usage(argv[0]);
                result = 2;
            } else {
                const unsigned maxPState =
                    cluster ==
                        kMBUClusterECPU0
                        ? 7U
                        : 17U;

                if (static_cast<unsigned>(
                        parsedPState) > maxPState) {

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
                    unsigned intervalMs = 10;

                    if (argc == 5) {
                        char *intervalEnd = nullptr;
                        const long parsedInterval =
                            std::strtol(
                                argv[4],
                                &intervalEnd,
                                10);

                        if (!intervalEnd ||
                            *intervalEnd != '\0' ||
                            parsedInterval < 1 ||
                            parsedInterval > 5000) {

                            std::fprintf(
                                stderr,
                                "interval must be 1..5000 ms\n");
                            IOServiceClose(connect);
                            return 2;
                        }

                        intervalMs =
                            static_cast<unsigned>(
                                parsedInterval);
                    }

                    if (cluster !=
                        kMBUClusterECPU0) {

                        std::fprintf(
                            stderr,
                            "warning: repeated P-cluster MMIO can panic "
                            "if that cluster power-gates between writes; "
                            "proceeding because you explicitly selected it.\n");
                    }

                    std::printf(
                        "holding %s at pstate %ld every %u ms\n"
                        "Ctrl-C to stop\n",
                        clusterName(
                            static_cast<unsigned>(
                                cluster)),
                        parsedPState,
                        intervalMs);

                    for (;;) {
                        if (setPState(
                                connect,
                                static_cast<unsigned>(
                                    cluster),
                                static_cast<unsigned>(
                                    parsedPState),
                                false) != 0) {

                            result = 1;
                            break;
                        }

                        usleep(
                            intervalMs * 1000);
                    }
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
