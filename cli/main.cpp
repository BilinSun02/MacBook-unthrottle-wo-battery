// SPDX-License-Identifier: GPL-2.0-only

#include <IOKit/IOKitLib.h>

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
printStatus(io_connect_t connect)
{
    MBUStatusReply reply{};

    size_t replySize =
        sizeof(reply);

    kern_return_t kr =
        IOConnectCallStructMethod(
            connect,
            kMBUSelectorGetStatus,
            nullptr,
            0,
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

    const bool mmioDisabled =
        (reply.flags &
         kMBUStatusFlagMMIODisabled) != 0;

    if (mmioDisabled) {
        std::printf(
            "MMIO access is disabled in this diagnostic build; "
            "status below contains static addresses/defaults only.\n");
    }

    for (unsigned i = 0;
         i < reply.cluster_count &&
         i < kMBUClusterCount;
         ++i) {

        const auto &s =
            reply.clusters[i];

        if (mmioDisabled) {
            std::printf(
                "%s\n"
                "  base          0x%llx\n"
                "  cmd_phys      0x%llx\n"
                "  requested_ps  unknown\n"
                "  m1n1_default  %u\n",
                clusterName(i),
                static_cast<
                    unsigned long long>(
                        s.cluster_base),
                static_cast<
                    unsigned long long>(
                        s.command_phys),
                s.default_pstate);
        } else {
            std::printf(
                "%s\n"
                "  base          0x%llx\n"
                "  cmd_phys      0x%llx\n"
                "  requested_ps  %u\n"
                "  m1n1_default  %u\n"
                "  cmd           0x%016llx\n"
                "  status        0x%016llx\n"
                "  last_change   0x%016llx\n"
                "  pll_status    0x%016llx\n"
                "  pll_factor    0x%016llx\n",
                clusterName(i),
                static_cast<
                    unsigned long long>(
                        s.cluster_base),
                static_cast<
                    unsigned long long>(
                        s.command_phys),
                s.requested_pstate,
                s.default_pstate,
                static_cast<
                    unsigned long long>(
                        s.raw_command),
                static_cast<
                    unsigned long long>(
                        s.raw_status),
                static_cast<
                    unsigned long long>(
                        s.last_change),
                static_cast<
                    unsigned long long>(
                        s.pll_status),
                static_cast<
                    unsigned long long>(
                        s.pll_factor));
        }
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
        "  %s status\n"
        "  %s restore-default\n"
        "  %s hold-default [milliseconds]\n",
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

        result =
            printStatus(connect);
    }

    else if (std::strcmp(
                 argv[1],
                 "restore-default") == 0) {

        result =
            restoreDefaults(connect);

        if (result == 0)
            result =
                printStatus(connect);
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
