// SPDX-License-Identifier: GPL-2.0-only

#include "MBUnthrottleUserClient.hpp"
#include "MBUnthrottleService.hpp"

#include <IOKit/IOLib.h>

#define super IOUserClient
OSDefineMetaClassAndStructors(MBUnthrottleUserClient, IOUserClient)

const IOExternalMethodDispatch
MBUnthrottleUserClient::methods_[kMBUSelectorCount] = {
    {
        &MBUnthrottleUserClient::sGetStatus,
        0,
        0,
        0,
        sizeof(MBUStatusReply),
    },
};

bool MBUnthrottleUserClient::initWithTask(task_t owningTask,
                                          void *securityID,
                                          UInt32 type,
                                          OSDictionary *properties)
{
    if (!super::initWithTask(owningTask, securityID, type, properties))
        return false;

    return clientHasPrivilege(owningTask, kIOClientPrivilegeAdministrator)
        == kIOReturnSuccess;
}

bool MBUnthrottleUserClient::start(IOService *provider)
{
    owner_ = OSDynamicCast(MBUnthrottleService, provider);
    if (!owner_)
        return false;

    if (!super::start(provider)) {
        owner_ = nullptr;
        return false;
    }

    return true;
}

IOReturn MBUnthrottleUserClient::clientClose()
{
    terminate();
    return kIOReturnSuccess;
}

IOReturn MBUnthrottleUserClient::externalMethod(
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
        const_cast<IOExternalMethodDispatch *>(&methods_[selector]),
        this,
        nullptr);
}

IOReturn MBUnthrottleUserClient::sGetStatus(
    OSObject *target,
    void *,
    IOExternalMethodArguments *arguments)
{
    auto *self = OSDynamicCast(MBUnthrottleUserClient, target);
    if (!self || !self->owner_ || !arguments ||
        !arguments->structureOutput ||
        arguments->structureOutputSize < sizeof(MBUStatusReply)) {
        return kIOReturnBadArgument;
    }

    auto *reply = static_cast<MBUStatusReply *>(arguments->structureOutput);
    const IOReturn ret = self->owner_->copyStatus(reply);
    if (ret == kIOReturnSuccess)
        arguments->structureOutputSize = sizeof(*reply);
    return ret;
}
