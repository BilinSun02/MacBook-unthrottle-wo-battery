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
    kMBUSelectorSetPState = 1,
    kMBUSelectorRestoreDefaults = 2,
    kMBUSelectorCount = 3,
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
    uint32_t reserved;
};

struct MBUStatusReply {
    uint32_t protocol_version;
    uint32_t cluster_count;
    MBUClusterStatus clusters[kMBUClusterCount];
};

struct MBUSetPStateRequest {
    uint32_t cluster;
    uint32_t pstate;
};

static constexpr uint32_t kMBUProtocolVersion = 1;
