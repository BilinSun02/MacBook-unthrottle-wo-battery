// SPDX-License-Identifier: GPL-2.0-only

#include "MBUnthrottleUserClient.hpp"
#include "MBUnthrottleService.hpp"

#include <IOKit/IOLib.h>

#define super IOUserClient
OSDefineMetaClassAndStructors(
    MBUnthrottleUserClient,
    IOUserClient)

const IOExternalMethodDispatch
MBUnthrottleUserClient::
methods_[kMBUSelectorCount] = {

    /*
     * get status
     */
    {
        &MBUnthrottleUserClient::sGetStatus,
        0,
        sizeof(MBUStatusRequest),
        0,
        sizeof(MBUStatusReply),
    },

    /*
     * restore known T6020 defaults
     */
    {
        &MBUnthrottleUserClient::
            sRestoreDefaults,
        0,
        0,
        0,
        0,
    },

    /*
     * read one explicitly selected 64-bit register
     */
    {
        &MBUnthrottleUserClient::
            sReadRegister,
        0,
        sizeof(MBUReadRequest),
        0,
        sizeof(MBUReadReply),
    },
};

bool
MBUnthrottleUserClient::initWithTask(
    task_t owningTask,
    void *securityID,
    UInt32 type,
    OSDictionary *properties)
{
    if (!super::initWithTask(
            owningTask,
            securityID,
            type,
            properties))
        return false;

    /*
     * Restrict the MMIO-changing method to an
     * administrator/root client.
     */
    return
        clientHasPrivilege(
            securityID,
            kIOClientPrivilegeAdministrator)
        == kIOReturnSuccess;
}

bool
MBUnthrottleUserClient::start(
    IOService *provider)
{
    owner_ =
        OSDynamicCast(
            MBUnthrottleService,
            provider);

    if (!owner_)
        return false;

    if (!super::start(provider)) {
        owner_ = nullptr;
        return false;
    }

    return true;
}

IOReturn
MBUnthrottleUserClient::clientClose()
{
    terminate();

    return kIOReturnSuccess;
}

IOReturn
MBUnthrottleUserClient::externalMethod(
    uint32_t selector,
    IOExternalMethodArguments *arguments,
    IOExternalMethodDispatch *,
    OSObject *,
    void *)
{
    if (selector >= kMBUSelectorCount)
        return kIOReturnUnsupported;

    return super::externalMethod(
        selector,
        arguments,
        const_cast<
            IOExternalMethodDispatch *>(
                &methods_[selector]),
        this,
        nullptr);
}

IOReturn
MBUnthrottleUserClient::sGetStatus(
    OSObject *target,
    void *,
    IOExternalMethodArguments *arguments)
{
    auto *self =
        OSDynamicCast(
            MBUnthrottleUserClient,
            target);

    if (!self ||
        !self->owner_ ||
        !arguments ||
        !arguments->structureInput ||
        arguments->structureInputSize
            != sizeof(MBUStatusRequest) ||
        !arguments->structureOutput ||
        arguments->structureOutputSize
            < sizeof(MBUStatusReply)) {

        return kIOReturnBadArgument;
    }

    const auto *request =
        static_cast<
            const MBUStatusRequest *>(
                arguments->structureInput);

    auto *reply =
        static_cast<
            MBUStatusReply *>(
                arguments->structureOutput);

    const IOReturn ret =
        self->owner_->copyStatus(
            request,
            reply);

    if (ret == kIOReturnSuccess) {

        arguments->structureOutputSize =
            sizeof(*reply);
    }

    return ret;
}

IOReturn
MBUnthrottleUserClient::sRestoreDefaults(
    OSObject *target,
    void *,
    IOExternalMethodArguments *)
{
    auto *self =
        OSDynamicCast(
            MBUnthrottleUserClient,
            target);

    if (!self || !self->owner_)
        return kIOReturnBadArgument;

    return
        self->owner_->restoreDefaults();
}


IOReturn
MBUnthrottleUserClient::sReadRegister(
    OSObject *target,
    void *,
    IOExternalMethodArguments *arguments)
{
    auto *self =
        OSDynamicCast(
            MBUnthrottleUserClient,
            target);

    if (!self ||
        !self->owner_ ||
        !arguments ||
        !arguments->structureInput ||
        arguments->structureInputSize
            != sizeof(MBUReadRequest) ||
        !arguments->structureOutput ||
        arguments->structureOutputSize
            < sizeof(MBUReadReply)) {

        return kIOReturnBadArgument;
    }

    const auto *request =
        static_cast<
            const MBUReadRequest *>(
                arguments->structureInput);

    auto *reply =
        static_cast<
            MBUReadReply *>(
                arguments->structureOutput);

    const IOReturn ret =
        self->owner_->readRegister(
            request,
            reply);

    if (ret == kIOReturnSuccess)
        arguments->structureOutputSize =
            sizeof(*reply);

    return ret;
}
