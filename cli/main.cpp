// SPDX-License-Identifier: GPL-2.0-only

#include <IOKit/IOKitLib.h>
#include <mach/mach_error.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../include/MBUProtocol.h"

static const char *clusterName(unsigned i)
{
    switch (i) {
        case kMBUClusterECPU0: return "ECPU0";
        case kMBUClusterPCPU0: return "PCPU0";
        case kMBUClusterPCPU1: return "PCPU1";
        default: return "?";
    }
}

int main(int argc, char **argv)
{
    if (argc != 2 || std::strcmp(argv[1], "status") != 0) {
        std::fprintf(stderr, "usage: %s status\n", argv[0]);
        return 2;
    }

    io_service_t service = IOServiceGetMatchingService(
        kIOMainPortDefault,
        IOServiceMatching("MBUnthrottleService"));

    if (!service) {
        std::fprintf(stderr,
            "MBUnthrottleService not found. Is the kext loaded?\n");
        return 1;
    }

    io_connect_t connect = IO_OBJECT_NULL;
    kern_return_t kr = IOServiceOpen(
        service, mach_task_self(), 0, &connect);
    IOObjectRelease(service);

    if (kr != KERN_SUCCESS) {
        std::fprintf(stderr, "IOServiceOpen: %s\n", mach_error_string(kr));
        return 1;
    }

    MBUStatusReply reply{};
    size_t replySize = sizeof(reply);

    kr = IOConnectCallStructMethod(
        connect,
        kMBUSelectorGetStatus,
        nullptr,
        0,
        &reply,
        &replySize);

    IOServiceClose(connect);

    if (kr != KERN_SUCCESS) {
        std::fprintf(stderr,
            "status call failed: %s (0x%x)\n",
            mach_error_string(kr), kr);
        return 1;
    }

    if (replySize != sizeof(reply) ||
        reply.protocol_version != kMBUProtocolVersion) {
        std::fprintf(stderr, "protocol mismatch\n");
        return 1;
    }

    std::printf("protocol=%u clusters=%u\n",
                reply.protocol_version, reply.cluster_count);

    for (unsigned i = 0;
         i < reply.cluster_count && i < kMBUClusterCount;
         ++i) {
        const auto &s = reply.clusters[i];
        std::printf(
            "%s base=0x%llx cmd_phys=0x%llx requested_ps=%u\n"
            "  cmd=0x%016llx status=0x%016llx last_change=0x%016llx\n"
            "  pll_status=0x%016llx pll_factor=0x%016llx\n",
            clusterName(i),
            static_cast<unsigned long long>(s.cluster_base),
            static_cast<unsigned long long>(s.command_phys),
            s.requested_pstate,
            static_cast<unsigned long long>(s.raw_command),
            static_cast<unsigned long long>(s.raw_status),
            static_cast<unsigned long long>(s.last_change),
            static_cast<unsigned long long>(s.pll_status),
            static_cast<unsigned long long>(s.pll_factor));
    }

    return 0;
}
