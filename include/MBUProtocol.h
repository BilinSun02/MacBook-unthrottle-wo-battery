// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#ifdef KERNEL
#include <sys/types.h>
#else
#include <stdint.h>
#endif

enum MBUCluster : uint32_t {
    kMBUClusterECPU0 = 0,
    kMBUClusterPCPU0 = 1,
    kMBUClusterPCPU1 = 2,
    kMBUClusterCount = 3,
};

enum MBUSelector : uint32_t {
    kMBUSelectorGetStatus = 0,
    kMBUSelectorRestoreDefaults = 1,
    kMBUSelectorCount = 2,
};

enum MBUStatusFlags : uint32_t {
    /*
     * Only clusters known to have survived MMIO reads in the v3
     * panic path are touched. PCPU1 remains completely untouched.
     */
    kMBUStatusFlagMMIOPartial = 1U << 0,
    kMBUStatusFlagWritesDisabled = 1U << 1,
};

enum MBUClusterFlags : uint32_t {
    kMBUClusterFlagMMIOMapped = 1U << 0,
    kMBUClusterFlagMMIORead = 1U << 1,
    kMBUClusterFlagSkippedUnavailable = 1U << 2,
};

struct MBUClusterStatus {
    uint64_t cluster_base;
    uint64_t command_phys;

    uint64_t raw_command;
    uint64_t raw_status;
    uint64_t last_change;
    uint64_t pll_status;
    uint64_t pll_factor;

    uint32_t requested_pstate;
    uint32_t default_pstate;
    uint32_t flags;
    uint32_t reserved;
};

struct MBUStatusReply {
    uint32_t protocol_version;
    uint32_t cluster_count;
    uint32_t flags;
    uint32_t reserved;
    MBUClusterStatus clusters[kMBUClusterCount];
};

static constexpr uint32_t kMBUProtocolVersion = 4;
