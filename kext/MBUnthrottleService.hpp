// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <IOKit/IOService.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOMemoryMap.h>
#include "../include/MBUProtocol.h"

class MBUnthrottleUserClient;

class MBUnthrottleService : public IOService {
    OSDeclareDefaultStructors(MBUnthrottleService)

public:
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    void free() override;

    IOReturn newUserClient(task_t owningTask,
                           void *securityID,
                           UInt32 type,
                           OSDictionary *properties,
                           IOUserClient **handler) override;

    IOReturn copyStatus(MBUStatusReply *reply);
    IOReturn setPState(uint32_t cluster, uint32_t pstate);
    IOReturn restoreDefaults();

private:
    struct ClusterMap {
        const char *name;
        uint64_t clusterBase;
        uint32_t defaultPState;
        IOMemoryDescriptor *descriptor;
        IOMemoryMap *mapping;
        volatile uint8_t *regs;
    };

    static constexpr uint64_t kDVFSPageOffset = 0x20000;
    static constexpr uint64_t kDVFSPageLength = 0x1000;

    static constexpr uint32_t kCmdOffset = 0x20;
    static constexpr uint32_t kLastChangeOffset = 0x38;
    static constexpr uint32_t kStatusOffset = 0x50;
    static constexpr uint32_t kPLLStatusOffset = 0xc0;
    static constexpr uint32_t kPLLFactorOffset = 0xc8;

    static constexpr uint64_t kCmdBusy = (1ULL << 31);
    static constexpr uint64_t kCmdSet = (1ULL << 25);
    static constexpr uint64_t kCmdPStateMask = 0x1fULL;

    // m1n1 src/cpufreq.c, t6020_clusters[]
    ClusterMap clusters_[kMBUClusterCount] = {
        {"ECPU0", 0x210e00000ULL, 5, nullptr, nullptr, nullptr},
        {"PCPU0", 0x211e00000ULL, 6, nullptr, nullptr, nullptr},
        {"PCPU1", 0x212e00000ULL, 6, nullptr, nullptr, nullptr},
    };

    bool runningOnT6020();
    bool mapCluster(ClusterMap &cluster);
    void unmapCluster(ClusterMap &cluster);

    uint64_t read64(const ClusterMap &cluster, uint32_t offset) const;
    void write64(const ClusterMap &cluster, uint32_t offset, uint64_t value) const;

    IOReturn setPStateLocked(ClusterMap &cluster, uint32_t pstate);
};
