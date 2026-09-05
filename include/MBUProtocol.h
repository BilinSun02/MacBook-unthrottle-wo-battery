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

static constexpr uint32_t kMBUClusterMaskECPU0 =
    1U << kMBUClusterECPU0;
static constexpr uint32_t kMBUClusterMaskPCPU0 =
    1U << kMBUClusterPCPU0;
static constexpr uint32_t kMBUClusterMaskPCPU1 =
    1U << kMBUClusterPCPU1;
static constexpr uint32_t kMBUClusterMaskAll =
    (1U << kMBUClusterCount) - 1U;

enum MBURegister : uint32_t {
    kMBURegisterCmd = 0,
    kMBURegisterLastChange = 1,
    kMBURegisterStatus = 2,
    kMBURegisterPLLStatus = 3,
    kMBURegisterPLLFactor = 4,
    kMBURegisterCount = 5,
};

enum MBUSelector : uint32_t {
    kMBUSelectorGetStatus = 0,
    kMBUSelectorRestoreDefaults = 1,
    kMBUSelectorReadRegister = 2,
    kMBUSelectorCount = 3,
};

enum MBUStatusFlags : uint32_t {
    kMBUStatusFlagWritesDisabled = 1U << 0,
    kMBUStatusFlagMetadataOnly = 1U << 1,
};

enum MBUClusterFlags : uint32_t {
    kMBUClusterFlagSelected = 1U << 0,
};

struct MBUStatusRequest {
    uint32_t cluster_mask;
    uint32_t reserved;
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

struct MBUReadRequest {
    uint32_t cluster;
    uint32_t reg;
};

struct MBUReadReply {
    uint32_t protocol_version;
    uint32_t cluster;
    uint32_t reg;
    uint32_t reserved;
    uint64_t physical_address;
    uint64_t value;
};

static constexpr uint32_t kMBUProtocolVersion = 7;
