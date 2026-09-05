// SPDX-License-Identifier: GPL-2.0-only

#include "MBUnthrottleService.hpp"
#include "MBUnthrottleUserClient.hpp"

#include <IOKit/IOLib.h>
#include <IOKit/IODeviceTreeSupport.h>

#include <libkern/c++/OSData.h>
#include <libkern/libkern.h>

#define super IOService
OSDefineMetaClassAndStructors(MBUnthrottleService, IOService)

static bool
dataContainsCString(OSData *data, const char *needle)
{
    if (!data || !needle)
        return false;

    const auto *bytes =
        static_cast<const uint8_t *>(data->getBytesNoCopy());

    const size_t len = data->getLength();
    const size_t needleLen = strlen(needle);

    if (!bytes || needleLen == 0 || len < needleLen)
        return false;

    for (size_t i = 0;
         i + needleLen <= len;
         ++i) {

        if (memcmp(bytes + i,
                   needle,
                   needleLen) == 0)
            return true;
    }

    return false;
}

bool
MBUnthrottleService::runningOnT6020()
{
    /*
     * The Apple SoC compatibility string lives on the arm-io
     * device-tree node, not on the device-tree root.
     *
     * macOS equivalent:
     *   ioreg -n arm-io -r -l | grep compatible
     */
    IORegistryEntry *armio =
        IORegistryEntry::fromPath("/arm-io",
                                  gIODTPlane);

    if (!armio) {
        IOLog(
            "MBUnthrottle: IODeviceTree:/arm-io not found\n");
        return false;
    }

    OSData *compatible =
        OSDynamicCast(
            OSData,
            armio->getProperty("compatible"));

    if (!compatible) {
        IOLog(
            "MBUnthrottle: arm-io has no compatible OSData\n");
        armio->release();
        return false;
    }

    const bool ok =
        dataContainsCString(
            compatible,
            "apple,t6020");

    IOLog(
        "MBUnthrottle: arm-io T6020 check: %s\n",
        ok ? "yes" : "no");

    armio->release();

    return ok;
}

bool
MBUnthrottleService::mapCluster(
    ClusterMap &cluster)
{
    const IOPhysicalAddress phys =
        static_cast<IOPhysicalAddress>(
            cluster.clusterBase
            + kDVFSPageOffset);

    cluster.descriptor =
        IOMemoryDescriptor::withPhysicalAddress(
            phys,
            kDVFSPageLength,
            kIODirectionInOut);

    if (!cluster.descriptor) {

        IOLog(
            "MBUnthrottle: %s: cannot create "
            "descriptor for 0x%llx\n",
            cluster.name,
            static_cast<unsigned long long>(
                phys));

        return false;
    }

    cluster.mapping =
        cluster.descriptor->map(
            kIOMapAnywhere |
            kIOMapInhibitCache);

    if (!cluster.mapping) {

        IOLog(
            "MBUnthrottle: %s: cannot map "
            "DVFS page\n",
            cluster.name);

        cluster.descriptor->release();
        cluster.descriptor = nullptr;

        return false;
    }

    cluster.regs =
        reinterpret_cast<volatile uint8_t *>(
            static_cast<uintptr_t>(
                cluster.mapping
                    ->getVirtualAddress()));

    if (!cluster.regs) {

        cluster.mapping->release();
        cluster.mapping = nullptr;

        cluster.descriptor->release();
        cluster.descriptor = nullptr;

        return false;
    }

    IOLog(
        "MBUnthrottle: %s: mapped "
        "0x%llx -> %p\n",
        cluster.name,
        static_cast<unsigned long long>(phys),
        const_cast<uint8_t *>(cluster.regs));

    return true;
}

void
MBUnthrottleService::unmapCluster(
    ClusterMap &cluster)
{
    cluster.regs = nullptr;

    if (cluster.mapping) {
        cluster.mapping->release();
        cluster.mapping = nullptr;
    }

    if (cluster.descriptor) {
        cluster.descriptor->release();
        cluster.descriptor = nullptr;
    }
}

uint64_t
MBUnthrottleService::read64(
    const ClusterMap &cluster,
    uint32_t offset) const
{
    auto *p =
        reinterpret_cast<
            volatile const uint64_t *>(
                cluster.regs + offset);

    const uint64_t value = *p;

    OSSynchronizeIO();

    return value;
}

void
MBUnthrottleService::write64(
    const ClusterMap &cluster,
    uint32_t offset,
    uint64_t value) const
{
    auto *p =
        reinterpret_cast<
            volatile uint64_t *>(
                const_cast<
                    volatile uint8_t *>(
                        cluster.regs)
                + offset);

    *p = value;

    OSSynchronizeIO();
}

IOReturn
MBUnthrottleService::setKnownDefaultPState(
    ClusterMap &cluster)
{
    if (!cluster.regs)
        return kIOReturnNotReady;

    /*
     * Match the Asahi Linux driver's transition
     * timeout:
     *
     *     poll every 2 us
     *     timeout around 400 us
     */
    uint64_t command = 0;

    bool ready = false;

    for (unsigned i = 0;
         i < 200;
         ++i) {

        command =
            read64(cluster,
                   kCmdOffset);

        if ((command & kCmdBusy) == 0) {
            ready = true;
            break;
        }

        IODelay(2);
    }

    if (!ready) {

        IOLog(
            "MBUnthrottle: %s: "
            "DVFS command remained busy\n",
            cluster.name);

        return kIOReturnTimeout;
    }

    const uint64_t before = command;

    /*
     * Absolutely do not build the register from
     * zero.
     *
     * Preserve all unknown/control bits, replace
     * only Desired1[4:0].
     */
    command &=
        ~kCmdPStateMask;

    command |=
        static_cast<uint64_t>(
            cluster.defaultPState)
        & kCmdPStateMask;

    /*
     * Submit request.
     */
    command |= kCmdSet;

    IOLog(
        "MBUnthrottle: %s: "
        "CMD before=0x%llx "
        "after=0x%llx "
        "request=%u\n",
        cluster.name,
        static_cast<unsigned long long>(
            before),
        static_cast<unsigned long long>(
            command),
        cluster.defaultPState);

    write64(
        cluster,
        kCmdOffset,
        command);

    /*
     * Do not assume success merely because the
     * MMIO store completed.
     *
     * Wait for BUSY to clear again.
     */
    for (unsigned i = 0;
         i < 200;
         ++i) {

        const uint64_t now =
            read64(cluster,
                   kCmdOffset);

        if ((now & kCmdBusy) == 0)
            return kIOReturnSuccess;

        IODelay(2);
    }

    IOLog(
        "MBUnthrottle: %s: "
        "post-write transition timeout\n",
        cluster.name);

    return kIOReturnTimeout;
}

bool
MBUnthrottleService::start(
    IOService *provider)
{
    if (!super::start(provider))
        return false;

    if (!runningOnT6020()) {

        IOLog(
            "MBUnthrottle: refusing to start: "
            "not apple,t6020\n");

        return false;
    }

    for (uint32_t i = 0;
         i < kMBUClusterCount;
         ++i) {

        if (!mapCluster(
                clusters_[i])) {

            for (uint32_t j = 0;
                 j < i;
                 ++j) {

                unmapCluster(
                    clusters_[j]);
            }

            return false;
        }
    }

    registerService();

    IOLog(
        "MBUnthrottle: started "
        "on T6020\n");

    return true;
}

void
MBUnthrottleService::stop(
    IOService *provider)
{
    for (uint32_t i = 0;
         i < kMBUClusterCount;
         ++i) {

        unmapCluster(
            clusters_[i]);
    }

    super::stop(provider);
}

void
MBUnthrottleService::free()
{
    for (uint32_t i = 0;
         i < kMBUClusterCount;
         ++i) {

        unmapCluster(
            clusters_[i]);
    }

    super::free();
}

IOReturn
MBUnthrottleService::newUserClient(
    task_t owningTask,
    void *securityID,
    UInt32 type,
    OSDictionary *properties,
    IOUserClient **handler)
{
    if (!handler)
        return kIOReturnBadArgument;

    *handler = nullptr;

    auto *client =
        new MBUnthrottleUserClient;

    if (!client)
        return kIOReturnNoMemory;

    if (!client->initWithTask(
            owningTask,
            securityID,
            type,
            properties) ||

        !client->attach(this) ||

        !client->start(this)) {

        client->detach(this);
        client->release();

        return kIOReturnError;
    }

    *handler = client;

    return kIOReturnSuccess;
}

IOReturn
MBUnthrottleService::copyStatus(
    MBUStatusReply *reply)
{
    if (!reply)
        return kIOReturnBadArgument;

    bzero(reply,
          sizeof(*reply));

    reply->protocol_version =
        kMBUProtocolVersion;

    reply->cluster_count =
        kMBUClusterCount;

    for (uint32_t i = 0;
         i < kMBUClusterCount;
         ++i) {

        const ClusterMap &c =
            clusters_[i];

        if (!c.regs)
            return kIOReturnNotReady;

        auto &s =
            reply->clusters[i];

        s.cluster_base =
            c.clusterBase;

        s.command_phys =
            c.clusterBase
            + kDVFSPageOffset
            + kCmdOffset;

        s.raw_command =
            read64(
                c,
                kCmdOffset);

        s.raw_status =
            read64(
                c,
                kStatusOffset);

        s.last_change =
            read64(
                c,
                kLastChangeOffset);

        s.pll_status =
            read64(
                c,
                kPLLStatusOffset);

        s.pll_factor =
            read64(
                c,
                kPLLFactorOffset);

        s.requested_pstate =
            static_cast<uint32_t>(
                s.raw_command
                & kCmdPStateMask);

        s.default_pstate =
            c.defaultPState;
    }

    return kIOReturnSuccess;
}

IOReturn
MBUnthrottleService::restoreDefaults()
{
    /*
     * Deliberately restrict writes to the known
     * m1n1 T6020 defaults.
     */
    for (uint32_t i = 0;
         i < kMBUClusterCount;
         ++i) {

        const IOReturn ret =
            setKnownDefaultPState(
                clusters_[i]);

        if (ret != kIOReturnSuccess)
            return ret;
    }

    return kIOReturnSuccess;
}
