// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <IOKit/IOUserClient.h>

#include "../include/MBUProtocol.h"

class MBUnthrottleService;

class MBUnthrottleUserClient :
    public IOUserClient {

    OSDeclareDefaultStructors(
        MBUnthrottleUserClient)

public:

    bool initWithTask(
        task_t owningTask,
        void *securityID,
        UInt32 type,
        OSDictionary *properties)
        override;

    bool start(
        IOService *provider)
        override;

    IOReturn clientClose()
        override;

    IOReturn externalMethod(
        uint32_t selector,
        IOExternalMethodArguments *arguments,
        IOExternalMethodDispatch *dispatch = nullptr,
        OSObject *target = nullptr,
        void *reference = nullptr)
        override;

private:

    MBUnthrottleService *owner_ =
        nullptr;

    static const
    IOExternalMethodDispatch
    methods_[kMBUSelectorCount];

    static IOReturn sGetStatus(
        OSObject *target,
        void *reference,
        IOExternalMethodArguments *arguments);

    static IOReturn sRestoreDefaults(
        OSObject *target,
        void *reference,
        IOExternalMethodArguments *arguments);

    static IOReturn sReadRegister(
        OSObject *target,
        void *reference,
        IOExternalMethodArguments *arguments);

    static IOReturn sSetPState(
        OSObject *target,
        void *reference,
        IOExternalMethodArguments *arguments);
};
