// SPDX-License-Identifier: GPL-2.0-only

#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>

#include <mach/mach_error.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
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

static int
clusterIndex(const char *name)
{
    if (!name)
        return -1;

    if (std::strcmp(name, "ECPU0") == 0)
        return kMBUClusterECPU0;

    if (std::strcmp(name, "PCPU0") == 0)
        return kMBUClusterPCPU0;

    if (std::strcmp(name, "PCPU1") == 0)
        return kMBUClusterPCPU1;

    return -1;
}

static bool
parseStatusMask(int argc,
                char **argv,
                uint32_t *mask)
{
    if (!mask || argc < 3)
        return false;

    *mask = 0;

    for (int argi = 2;
         argi < argc;
         ++argi) {

        const int cluster =
            clusterIndex(argv[argi]);

        if (cluster < 0) {
            std::fprintf(
                stderr,
                "unknown cluster: %s\n",
                argv[argi]);
            return false;
        }

        if (cluster == kMBUClusterPCPU1) {
            std::fprintf(
                stderr,
                "warning: PCPU1 previously caused an LLC Bus Error "
                "at 0x212e20020 while unavailable; proceeding because "
                "you explicitly selected it.\n");
        }

        *mask |=
            1U << static_cast<unsigned>(
                cluster);
    }

    return *mask != 0;
}

static const char *
registerName(unsigned r)
{
    switch (r) {
        case kMBURegisterCmd:
            return "cmd";
        case kMBURegisterLastChange:
            return "last_change";
        case kMBURegisterStatus:
            return "status";
        case kMBURegisterPLLStatus:
            return "pll_status";
        case kMBURegisterPLLFactor:
            return "pll_factor";
        default:
            return "?";
    }
}

static int
registerIndex(const char *name)
{
    if (!name)
        return -1;

    if (std::strcmp(name, "cmd") == 0)
        return kMBURegisterCmd;
    if (std::strcmp(name, "last_change") == 0)
        return kMBURegisterLastChange;
    if (std::strcmp(name, "status") == 0)
        return kMBURegisterStatus;
    if (std::strcmp(name, "pll_status") == 0)
        return kMBURegisterPLLStatus;
    if (std::strcmp(name, "pll_factor") == 0)
        return kMBURegisterPLLFactor;

    return -1;
}

static io_connect_t
openServiceConnection(const char *serviceClass,
                      uint32_t type)
{
    io_service_t service =
        IOServiceGetMatchingService(
            kIOMainPortDefault,
            IOServiceMatching(serviceClass));

    if (!service) {
        std::fprintf(
            stderr,
            "%s not found\n",
            serviceClass);
        return IO_OBJECT_NULL;
    }

    io_connect_t connect =
        IO_OBJECT_NULL;

    kern_return_t kr =
        IOServiceOpen(
            service,
            mach_task_self(),
            type,
            &connect);

    IOObjectRelease(service);

    if (kr != KERN_SUCCESS) {
        std::fprintf(
            stderr,
            "IOServiceOpen(%s,type=%u): %s (0x%x)\n",
            serviceClass,
            type,
            mach_error_string(kr),
            kr);
        return IO_OBJECT_NULL;
    }

    return connect;
}

static void
dumpNonZeroHex(const unsigned char *data,
               size_t size)
{
    for (size_t offset = 0;
         offset < size;
         offset += 16) {

        const size_t count =
            size - offset < 16
                ? size - offset
                : 16;

        bool nonzero = false;

        for (size_t i = 0;
             i < count;
             ++i) {

            if (data[offset + i] != 0) {
                nonzero = true;
                break;
            }
        }

        if (!nonzero)
            continue;

        std::printf(
            "%04zx:",
            offset);

        for (size_t i = 0;
             i < count;
             ++i) {

            std::printf(
                " %02x",
                data[offset + i]);
        }

        std::printf("\n");
    }
}

static bool
copyUInt32Property(io_registry_entry_t entry,
                   CFStringRef key,
                   uint32_t *value)
{
    if (!value)
        return false;

    CFTypeRef property =
        IORegistryEntryCreateCFProperty(
            entry,
            key,
            kCFAllocatorDefault,
            0);

    if (!property)
        return false;

    bool ok = false;

    if (CFGetTypeID(property) ==
        CFNumberGetTypeID()) {

        int64_t v = 0;

        if (CFNumberGetValue(
                static_cast<CFNumberRef>(
                    property),
                kCFNumberSInt64Type,
                &v) &&
            v >= 0 &&
            v <= 0xffffffffLL) {

            *value =
                static_cast<uint32_t>(v);

            ok = true;
        }
    }

    CFRelease(property);

    return ok;
}

static int
ppmSetPropertiesProbe()
{
    io_service_t service =
        IOServiceGetMatchingService(
            kIOMainPortDefault,
            IOServiceMatching(
                "ApplePassthroughPPM"));

    if (!service) {
        std::fprintf(
            stderr,
            "ApplePassthroughPPM not found\n");
        return 1;
    }

    static const CFStringRef key =
        CFSTR("UseOverrideBatteryInputV");

    uint32_t before = 0;

    if (!copyUInt32Property(
            service,
            key,
            &before)) {

        std::fprintf(
            stderr,
            "could not read UseOverrideBatteryInputV\n");

        IOObjectRelease(service);

        return 1;
    }

    std::printf(
        "UseOverrideBatteryInputV before=%u\n",
        before);

    if (before != 0) {
        std::fprintf(
            stderr,
            "refusing probe because the current value is not 0; "
            "this probe only re-submits an unchanged disabled override.\n");

        IOObjectRelease(service);

        return 1;
    }

    CFMutableDictionaryRef properties =
        CFDictionaryCreateMutable(
            kCFAllocatorDefault,
            0,
            &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks);

    if (!properties) {
        IOObjectRelease(service);
        return 1;
    }

    int32_t zero = 0;

    CFNumberRef zeroNumber =
        CFNumberCreate(
            kCFAllocatorDefault,
            kCFNumberSInt32Type,
            &zero);

    if (!zeroNumber) {
        CFRelease(properties);
        IOObjectRelease(service);
        return 1;
    }

    CFDictionarySetValue(
        properties,
        key,
        zeroNumber);

    kern_return_t kr =
        IORegistryEntrySetCFProperties(
            service,
            properties);

    CFRelease(zeroNumber);
    CFRelease(properties);

    if (kr != KERN_SUCCESS) {
        std::fprintf(
            stderr,
            "IORegistryEntrySetCFProperties: %s (0x%x)\n",
            mach_error_string(kr),
            kr);

        IOObjectRelease(service);

        return 1;
    }

    uint32_t after = 0;

    if (!copyUInt32Property(
            service,
            key,
            &after)) {

        std::fprintf(
            stderr,
            "setProperties succeeded but readback failed\n");

        IOObjectRelease(service);

        return 1;
    }

    IOObjectRelease(service);

    std::printf(
        "setProperties returned success\n"
        "UseOverrideBatteryInputV after=%u\n"
        "no effective value change was requested\n",
        after);

    return after == 0 ? 0 : 1;
}

static io_service_t
openPPMService();

static int
ppmFreshCellStatus()
{
    io_service_t service =
        openPPMService();

    if (!service)
        return 1;

    uint32_t value = 0;

    const bool ok =
        copyUInt32Property(
            service,
            CFSTR("UseFreshCellParamsForPmax"),
            &value);

    IOObjectRelease(service);

    if (!ok) {
        std::fprintf(
            stderr,
            "could not read UseFreshCellParamsForPmax\n");
        return 1;
    }

    std::printf(
        "UseFreshCellParamsForPmax=%u\n",
        value);

    return 0;
}

static int
ppmSetFreshCell(uint32_t value)
{
    if (value > 1) {
        std::fprintf(
            stderr,
            "fresh-cell value must be 0 or 1\n");
        return 2;
    }

    io_service_t service =
        openPPMService();

    if (!service)
        return 1;

    uint32_t before = 0;

    if (!copyUInt32Property(
            service,
            CFSTR("UseFreshCellParamsForPmax"),
            &before)) {

        std::fprintf(
            stderr,
            "could not read UseFreshCellParamsForPmax\n");

        IOObjectRelease(service);
        return 1;
    }

    CFMutableDictionaryRef properties =
        CFDictionaryCreateMutable(
            kCFAllocatorDefault,
            0,
            &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks);

    int32_t signedValue =
        static_cast<int32_t>(value);

    CFNumberRef number =
        CFNumberCreate(
            kCFAllocatorDefault,
            kCFNumberSInt32Type,
            &signedValue);

    if (!properties ||
        !number) {

        if (number)
            CFRelease(number);

        if (properties)
            CFRelease(properties);

        IOObjectRelease(service);
        return 1;
    }

    CFDictionarySetValue(
        properties,
        CFSTR("UseFreshCellParamsForPmax"),
        number);

    kern_return_t kr =
        IORegistryEntrySetCFProperties(
            service,
            properties);

    CFRelease(number);
    CFRelease(properties);

    if (kr != KERN_SUCCESS) {
        std::fprintf(
            stderr,
            "IORegistryEntrySetCFProperties: %s (0x%x)\n",
            mach_error_string(kr),
            kr);

        IOObjectRelease(service);
        return 1;
    }

    uint32_t after = 0;

    const bool gotAfter =
        copyUInt32Property(
            service,
            CFSTR("UseFreshCellParamsForPmax"),
            &after);

    IOObjectRelease(service);

    if (!gotAfter) {
        std::fprintf(
            stderr,
            "setProperties succeeded but readback failed\n");
        return 1;
    }

    std::printf(
        "UseFreshCellParamsForPmax before=%u after=%u\n",
        before,
        after);

    return after == value ? 0 : 1;
}

static int
ppmBaselineStatus()
{
    io_service_t service =
        openPPMService();

    if (!service)
        return 1;

    uint32_t value = 0;

    const bool ok =
        copyUInt32Property(
            service,
            CFSTR("UseBaselineSystemCapability"),
            &value);

    IOObjectRelease(service);

    if (!ok) {
        std::fprintf(
            stderr,
            "could not read UseBaselineSystemCapability\n");
        return 1;
    }

    std::printf(
        "UseBaselineSystemCapability=%u\n",
        value);

    return 0;
}

static int
ppmSetBaseline(uint32_t value)
{
    if (value > 1) {
        std::fprintf(
            stderr,
            "baseline value must be 0 or 1\n");
        return 2;
    }

    io_service_t service =
        openPPMService();

    if (!service)
        return 1;

    uint32_t before = 0;

    if (!copyUInt32Property(
            service,
            CFSTR("UseBaselineSystemCapability"),
            &before)) {

        std::fprintf(
            stderr,
            "could not read UseBaselineSystemCapability\n");

        IOObjectRelease(service);
        return 1;
    }

    CFMutableDictionaryRef properties =
        CFDictionaryCreateMutable(
            kCFAllocatorDefault,
            0,
            &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks);

    int32_t signedValue =
        static_cast<int32_t>(value);

    CFNumberRef number =
        CFNumberCreate(
            kCFAllocatorDefault,
            kCFNumberSInt32Type,
            &signedValue);

    if (!properties ||
        !number) {

        if (number)
            CFRelease(number);

        if (properties)
            CFRelease(properties);

        IOObjectRelease(service);
        return 1;
    }

    CFDictionarySetValue(
        properties,
        CFSTR("UseBaselineSystemCapability"),
        number);

    kern_return_t kr =
        IORegistryEntrySetCFProperties(
            service,
            properties);

    CFRelease(number);
    CFRelease(properties);

    if (kr != KERN_SUCCESS) {
        std::fprintf(
            stderr,
            "IORegistryEntrySetCFProperties: %s (0x%x)\n",
            mach_error_string(kr),
            kr);

        IOObjectRelease(service);
        return 1;
    }

    uint32_t after = 0;

    const bool gotAfter =
        copyUInt32Property(
            service,
            CFSTR("UseBaselineSystemCapability"),
            &after);

    IOObjectRelease(service);

    if (!gotAfter) {
        std::fprintf(
            stderr,
            "setProperties succeeded but readback failed\n");
        return 1;
    }

    std::printf(
        "UseBaselineSystemCapability before=%u after=%u\n",
        before,
        after);

    return after == value ? 0 : 1;
}

static bool
copyUInt32ArrayProperty(io_registry_entry_t entry,
                        CFStringRef key,
                        uint32_t *values,
                        size_t capacity,
                        size_t *count)
{
    if (!values || !count || capacity == 0)
        return false;

    *count = 0;

    CFTypeRef property =
        IORegistryEntryCreateCFProperty(
            entry,
            key,
            kCFAllocatorDefault,
            0);

    if (!property)
        return false;

    bool ok = false;

    if (CFGetTypeID(property) ==
        CFArrayGetTypeID()) {

        CFArrayRef array =
            static_cast<CFArrayRef>(property);

        const CFIndex n =
            CFArrayGetCount(array);

        if (n >= 0 &&
            static_cast<size_t>(n) <= capacity) {

            ok = true;

            for (CFIndex i = 0;
                 i < n;
                 ++i) {

                CFTypeRef item =
                    CFArrayGetValueAtIndex(
                        array,
                        i);

                if (!item ||
                    CFGetTypeID(item) !=
                        CFNumberGetTypeID()) {

                    ok = false;
                    break;
                }

                int64_t v = 0;

                if (!CFNumberGetValue(
                        static_cast<CFNumberRef>(
                            item),
                        kCFNumberSInt64Type,
                        &v) ||
                    v < 0 ||
                    v > 0xffffffffLL) {

                    ok = false;
                    break;
                }

                values[i] =
                    static_cast<uint32_t>(v);
            }

            if (ok)
                *count =
                    static_cast<size_t>(n);
        }
    }

    CFRelease(property);

    return ok;
}

static void
printUInt32Array(const uint32_t *values,
                 size_t count)
{
    std::printf("(");

    for (size_t i = 0;
         i < count;
         ++i) {

        if (i)
            std::printf(",");

        std::printf(
            "%u",
            values[i]);
    }

    std::printf(")");
}

static io_service_t
openPPMService()
{
    io_service_t service =
        IOServiceGetMatchingService(
            kIOMainPortDefault,
            IOServiceMatching(
                "ApplePassthroughPPM"));

    if (!service) {
        std::fprintf(
            stderr,
            "ApplePassthroughPPM not found\n");
    }

    return service;
}

static int
ppmSyscapStatus()
{
    io_service_t service =
        openPPMService();

    if (!service)
        return 1;

    uint32_t useOverride = 0;

    uint32_t baseline[8]{};
    size_t baselineCount = 0;

    uint32_t overrideValues[8]{};
    size_t overrideCount = 0;

    const bool gotUse =
        copyUInt32Property(
            service,
            CFSTR("UseOverrideSystemCapability"),
            &useOverride);

    const bool gotBaseline =
        copyUInt32ArrayProperty(
            service,
            CFSTR("BaselineSystemCapability"),
            baseline,
            8,
            &baselineCount);

    const bool gotOverride =
        copyUInt32ArrayProperty(
            service,
            CFSTR("OverrideSystemCapability"),
            overrideValues,
            8,
            &overrideCount);

    IOObjectRelease(service);

    if (!gotUse ||
        !gotBaseline ||
        !gotOverride) {

        std::fprintf(
            stderr,
            "failed to read one or more system-capability properties\n");

        return 1;
    }

    std::printf(
        "UseOverrideSystemCapability=%u\n"
        "BaselineSystemCapability=",
        useOverride);

    printUInt32Array(
        baseline,
        baselineCount);

    std::printf(
        "\nOverrideSystemCapability=");

    printUInt32Array(
        overrideValues,
        overrideCount);

    std::printf("\n");

    return 0;
}

static CFMutableArrayRef
makeUInt32Array(uint32_t value,
                size_t count)
{
    CFMutableArrayRef array =
        CFArrayCreateMutable(
            kCFAllocatorDefault,
            static_cast<CFIndex>(count),
            &kCFTypeArrayCallBacks);

    if (!array)
        return nullptr;

    for (size_t i = 0;
         i < count;
         ++i) {

        int32_t signedValue =
            static_cast<int32_t>(value);

        CFNumberRef number =
            CFNumberCreate(
                kCFAllocatorDefault,
                kCFNumberSInt32Type,
                &signedValue);

        if (!number) {
            CFRelease(array);
            return nullptr;
        }

        CFArrayAppendValue(
            array,
            number);

        CFRelease(number);
    }

    return array;
}

static int
ppmSetSyscap(uint32_t value)
{
    io_service_t service =
        openPPMService();

    if (!service)
        return 1;

    uint32_t useOverride = 0;

    if (!copyUInt32Property(
            service,
            CFSTR("UseOverrideSystemCapability"),
            &useOverride)) {

        std::fprintf(
            stderr,
            "could not read UseOverrideSystemCapability\n");

        IOObjectRelease(service);

        return 1;
    }

    if (useOverride != 0) {
        std::fprintf(
            stderr,
            "refusing to replace an already-active system-capability override; "
            "clear it first\n");

        IOObjectRelease(service);

        return 1;
    }

    CFMutableDictionaryRef properties =
        CFDictionaryCreateMutable(
            kCFAllocatorDefault,
            0,
            &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks);

    CFMutableArrayRef values =
        makeUInt32Array(
            value,
            3);

    int32_t one = 1;

    CFNumberRef enable =
        CFNumberCreate(
            kCFAllocatorDefault,
            kCFNumberSInt32Type,
            &one);

    if (!properties ||
        !values ||
        !enable) {

        if (enable)
            CFRelease(enable);

        if (values)
            CFRelease(values);

        if (properties)
            CFRelease(properties);

        IOObjectRelease(service);

        return 1;
    }

    CFDictionarySetValue(
        properties,
        CFSTR("OverrideSystemCapability"),
        values);

    CFDictionarySetValue(
        properties,
        CFSTR("UseOverrideSystemCapability"),
        enable);

    kern_return_t kr =
        IORegistryEntrySetCFProperties(
            service,
            properties);

    CFRelease(enable);
    CFRelease(values);
    CFRelease(properties);
    IOObjectRelease(service);

    if (kr != KERN_SUCCESS) {
        std::fprintf(
            stderr,
            "IORegistryEntrySetCFProperties: %s (0x%x)\n",
            mach_error_string(kr),
            kr);

        return 1;
    }

    std::printf(
        "requested system-capability override %u mW on all 3 entries\n",
        value);

    return ppmSyscapStatus();
}

static int
ppmClearSyscap()
{
    io_service_t service =
        openPPMService();

    if (!service)
        return 1;

    CFMutableDictionaryRef properties =
        CFDictionaryCreateMutable(
            kCFAllocatorDefault,
            0,
            &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks);

    int32_t zero = 0;

    CFNumberRef disable =
        CFNumberCreate(
            kCFAllocatorDefault,
            kCFNumberSInt32Type,
            &zero);

    if (!properties ||
        !disable) {

        if (disable)
            CFRelease(disable);

        if (properties)
            CFRelease(properties);

        IOObjectRelease(service);

        return 1;
    }

    CFMutableArrayRef sentinel =
        makeUInt32Array(
            0x7fffffffU,
            3);

    if (!sentinel) {
        CFRelease(disable);
        CFRelease(properties);
        IOObjectRelease(service);
        return 1;
    }

    CFDictionarySetValue(
        properties,
        CFSTR("OverrideSystemCapability"),
        sentinel);

    CFDictionarySetValue(
        properties,
        CFSTR("UseOverrideSystemCapability"),
        disable);

    kern_return_t kr =
        IORegistryEntrySetCFProperties(
            service,
            properties);

    CFRelease(sentinel);
    CFRelease(disable);
    CFRelease(properties);
    IOObjectRelease(service);

    if (kr != KERN_SUCCESS) {
        std::fprintf(
            stderr,
            "IORegistryEntrySetCFProperties: %s (0x%x)\n",
            mach_error_string(kr),
            kr);

        return 1;
    }

    std::printf(
        "cleared system-capability override and restored INT32_MAX sentinel values\n");

    return ppmSyscapStatus();
}

typedef struct IOReportSubscriptionRefStruct *
    IOReportSubscriptionRef;

struct IOReportAPI {
    void *handle = nullptr;

    CFMutableDictionaryRef (*copyAllChannels)(
        uint64_t,
        uint64_t) = nullptr;

    CFDictionaryRef (*copyChannelsInGroup)(
        CFStringRef,
        CFStringRef,
        uint64_t,
        uint64_t,
        uint64_t) = nullptr;

    void (*mergeChannels)(
        CFDictionaryRef,
        CFDictionaryRef,
        CFTypeRef) = nullptr;

    IOReportSubscriptionRef (*createSubscription)(
        void *,
        CFMutableDictionaryRef,
        CFMutableDictionaryRef *,
        uint64_t,
        CFTypeRef) = nullptr;

    CFDictionaryRef (*createSamples)(
        IOReportSubscriptionRef,
        CFMutableDictionaryRef,
        CFTypeRef) = nullptr;

    CFDictionaryRef (*createSamplesDelta)(
        CFDictionaryRef,
        CFDictionaryRef,
        CFTypeRef) = nullptr;

    CFStringRef (*channelGetGroup)(
        CFDictionaryRef) = nullptr;

    CFStringRef (*channelGetSubGroup)(
        CFDictionaryRef) = nullptr;

    CFStringRef (*channelGetChannelName)(
        CFDictionaryRef) = nullptr;

    CFStringRef (*channelGetUnitLabel)(
        CFDictionaryRef) = nullptr;

    int32_t (*channelGetFormat)(
        CFDictionaryRef) = nullptr;

    int64_t (*simpleGetIntegerValue)(
        CFDictionaryRef,
        int32_t) = nullptr;

    int32_t (*stateGetCount)(
        CFDictionaryRef) = nullptr;

    CFStringRef (*stateGetNameForIndex)(
        CFDictionaryRef,
        int32_t) = nullptr;

    int64_t (*stateGetResidency)(
        CFDictionaryRef,
        int32_t) = nullptr;
};

template <typename T>
static bool
loadIOReportSymbol(void *handle,
                   const char *name,
                   T *out)
{
    if (!handle ||
        !name ||
        !out)
        return false;

    void *symbol =
        dlsym(
            handle,
            name);

    if (!symbol)
        return false;

    *out =
        reinterpret_cast<T>(symbol);

    return true;
}

static bool
loadIOReport(IOReportAPI *api)
{
    if (!api)
        return false;

    api->handle =
        dlopen(
            "/usr/lib/libIOReport.dylib",
            RTLD_LAZY | RTLD_LOCAL);

    if (!api->handle) {
        api->handle =
            dlopen(
                "/System/Library/PrivateFrameworks/IOReport.framework/IOReport",
                RTLD_LAZY | RTLD_LOCAL);
    }

    if (!api->handle) {
        std::fprintf(
            stderr,
            "could not load IOReport: %s\n",
            dlerror());

        return false;
    }

#define LOAD_IOREPORT(member, symbolName) \
    do { \
        if (!loadIOReportSymbol( \
                api->handle, \
                symbolName, \
                &api->member)) { \
            std::fprintf( \
                stderr, \
                "missing IOReport symbol: %s\n", \
                symbolName); \
            dlclose(api->handle); \
            api->handle = nullptr; \
            return false; \
        } \
    } while (0)

    LOAD_IOREPORT(
        copyAllChannels,
        "IOReportCopyAllChannels");

    LOAD_IOREPORT(
        copyChannelsInGroup,
        "IOReportCopyChannelsInGroup");

    LOAD_IOREPORT(
        mergeChannels,
        "IOReportMergeChannels");

    LOAD_IOREPORT(
        createSubscription,
        "IOReportCreateSubscription");

    LOAD_IOREPORT(
        createSamples,
        "IOReportCreateSamples");

    LOAD_IOREPORT(
        createSamplesDelta,
        "IOReportCreateSamplesDelta");

    LOAD_IOREPORT(
        channelGetGroup,
        "IOReportChannelGetGroup");

    LOAD_IOREPORT(
        channelGetSubGroup,
        "IOReportChannelGetSubGroup");

    LOAD_IOREPORT(
        channelGetChannelName,
        "IOReportChannelGetChannelName");

    LOAD_IOREPORT(
        channelGetUnitLabel,
        "IOReportChannelGetUnitLabel");

    LOAD_IOREPORT(
        channelGetFormat,
        "IOReportChannelGetFormat");

    LOAD_IOREPORT(
        simpleGetIntegerValue,
        "IOReportSimpleGetIntegerValue");

    LOAD_IOREPORT(
        stateGetCount,
        "IOReportStateGetCount");

    LOAD_IOREPORT(
        stateGetNameForIndex,
        "IOReportStateGetNameForIndex");

    LOAD_IOREPORT(
        stateGetResidency,
        "IOReportStateGetResidency");

#undef LOAD_IOREPORT

    return true;
}

static void
cfStringToCString(CFStringRef string,
                  char *buffer,
                  size_t capacity)
{
    if (!buffer ||
        capacity == 0)
        return;

    buffer[0] = '\0';

    if (!string)
        return;

    CFStringGetCString(
        string,
        buffer,
        static_cast<CFIndex>(
            capacity),
        kCFStringEncodingUTF8);
}

static int
ppmIOReport(unsigned intervalMs)
{
    IOReportAPI api{};

    if (!loadIOReport(&api))
        return 1;

    static const CFStringRef probeSubgroups[] = {
        CFSTR("Client2"),
        CFSTR("Client5"),
        CFSTR("Client6"),
    };

    for (CFStringRef subgroup :
         probeSubgroups) {

        CFDictionaryRef probe =
            api.copyChannelsInGroup(
                CFSTR("PPM Stats"),
                subgroup,
                0,
                0,
                0);

        char subgroupName[64]{};

        cfStringToCString(
            subgroup,
            subgroupName,
            sizeof(subgroupName));

        if (!probe) {
            std::printf(
                "IOReport subgroup query %s: null\n",
                subgroupName);
            continue;
        }

        CFTypeRef probeArrayObject =
            CFDictionaryGetValue(
                probe,
                CFSTR("IOReportChannels"));

        CFIndex probeCount = 0;

        if (probeArrayObject &&
            CFGetTypeID(probeArrayObject) ==
                CFArrayGetTypeID()) {

            CFArrayRef probeArray =
                static_cast<CFArrayRef>(
                    probeArrayObject);

            probeCount =
                CFArrayGetCount(
                    probeArray);

            std::printf(
                "IOReport subgroup query %s: %ld channels\n",
                subgroupName,
                static_cast<long>(
                    probeCount));

            for (CFIndex i = 0;
                 i < probeCount;
                 ++i) {

                CFTypeRef itemObject =
                    CFArrayGetValueAtIndex(
                        probeArray,
                        i);

                if (!itemObject ||
                    CFGetTypeID(itemObject) !=
                        CFDictionaryGetTypeID())
                    continue;

                CFDictionaryRef item =
                    static_cast<CFDictionaryRef>(
                        itemObject);

                char group[128]{};
                char actualSubgroup[128]{};
                char name[128]{};

                cfStringToCString(
                    api.channelGetGroup(item),
                    group,
                    sizeof(group));

                cfStringToCString(
                    api.channelGetSubGroup(item),
                    actualSubgroup,
                    sizeof(actualSubgroup));

                cfStringToCString(
                    api.channelGetChannelName(item),
                    name,
                    sizeof(name));

                std::printf(
                    "  [%s] [%s] %s format=%d\n",
                    group,
                    actualSubgroup,
                    name,
                    api.channelGetFormat(item));
            }
        }

        else {
            std::printf(
                "IOReport subgroup query %s: no IOReportChannels array\n",
                subgroupName);
        }

        CFRelease(probe);
    }

    CFMutableDictionaryRef channels =
        api.copyAllChannels(
            0,
            0);

    if (!channels) {
        std::fprintf(
            stderr,
            "IOReportCopyAllChannels returned null\n");

        dlclose(api.handle);

        return 1;
    }

    for (CFStringRef subgroup :
         probeSubgroups) {

        CFDictionaryRef clientChannels =
            api.copyChannelsInGroup(
                CFSTR("PPM Stats"),
                subgroup,
                0,
                0,
                0);

        if (!clientChannels)
            continue;

        api.mergeChannels(
            channels,
            clientChannels,
            nullptr);

        CFRelease(clientChannels);
    }

    CFTypeRef discoveredObject =
        CFDictionaryGetValue(
            channels,
            CFSTR("IOReportChannels"));

    CFIndex discoveredCount = 0;

    if (discoveredObject &&
        CFGetTypeID(discoveredObject) ==
            CFArrayGetTypeID()) {

        discoveredCount =
            CFArrayGetCount(
                static_cast<CFArrayRef>(
                    discoveredObject));
    }

    std::printf(
        "IOReport discovery: %ld total channels\n",
        static_cast<long>(
            discoveredCount));

    CFMutableDictionaryRef subscribed =
        nullptr;

    IOReportSubscriptionRef subscription =
        api.createSubscription(
            nullptr,
            channels,
            &subscribed,
            0,
            nullptr);

    if (!subscription) {
        std::fprintf(
            stderr,
            "IOReportCreateSubscription failed\n");

        if (subscribed)
            CFRelease(subscribed);

        CFRelease(channels);
        dlclose(api.handle);

        return 1;
    }

    CFMutableDictionaryRef sampleChannels =
        subscribed
            ? subscribed
            : channels;

    if (subscribed) {
        CFTypeRef subscribedObject =
            CFDictionaryGetValue(
                subscribed,
                CFSTR("IOReportChannels"));

        CFIndex subscribedCount = 0;

        if (subscribedObject &&
            CFGetTypeID(subscribedObject) ==
                CFArrayGetTypeID()) {

            subscribedCount =
                CFArrayGetCount(
                    static_cast<CFArrayRef>(
                        subscribedObject));
        }

        std::printf(
            "IOReport subscription accepted %ld channels\n",
            static_cast<long>(
                subscribedCount));
    }

    CFDictionaryRef first =
        api.createSamples(
            subscription,
            sampleChannels,
            nullptr);

    if (!first) {
        std::fprintf(
            stderr,
            "first IOReport sample failed\n");

        if (subscribed)
            CFRelease(subscribed);

        CFRelease(channels);
        dlclose(api.handle);

        return 1;
    }

    usleep(
        intervalMs * 1000U);

    CFDictionaryRef second =
        api.createSamples(
            subscription,
            sampleChannels,
            nullptr);

    if (!second) {
        std::fprintf(
            stderr,
            "second IOReport sample failed\n");

        CFRelease(first);

        if (subscribed)
            CFRelease(subscribed);

        CFRelease(channels);
        dlclose(api.handle);

        return 1;
    }

    CFDictionaryRef delta =
        api.createSamplesDelta(
            first,
            second,
            nullptr);

    CFRelease(first);
    CFRelease(second);

    if (!delta) {
        std::fprintf(
            stderr,
            "IOReportCreateSamplesDelta failed\n");

        if (subscribed)
            CFRelease(subscribed);

        CFRelease(channels);
        dlclose(api.handle);

        return 1;
    }

    CFTypeRef arrayObject =
        CFDictionaryGetValue(
            delta,
            CFSTR("IOReportChannels"));

    if (!arrayObject ||
        CFGetTypeID(arrayObject) !=
            CFArrayGetTypeID()) {

        std::fprintf(
            stderr,
            "PPM Stats delta has no IOReportChannels array\n");

        CFRelease(delta);

        if (subscribed)
            CFRelease(subscribed);

        CFRelease(channels);
        dlclose(api.handle);

        return 1;
    }

    CFArrayRef array =
        static_cast<CFArrayRef>(
            arrayObject);

    const CFIndex count =
        CFArrayGetCount(array);

    std::printf(
        "IOReport delta over %u ms: %ld total sampled channels; printing PPM Stats/BgtIdx only\n",
        intervalMs,
        static_cast<long>(
            count));

    for (CFIndex i = 0;
         i < count;
         ++i) {

        CFTypeRef itemObject =
            CFArrayGetValueAtIndex(
                array,
                i);

        if (!itemObject ||
            CFGetTypeID(itemObject) !=
                CFDictionaryGetTypeID())
            continue;

        CFDictionaryRef item =
            static_cast<CFDictionaryRef>(
                itemObject);

        char group[128]{};
        char subgroup[128]{};
        char name[128]{};
        char unit[64]{};

        cfStringToCString(
            api.channelGetGroup(item),
            group,
            sizeof(group));

        cfStringToCString(
            api.channelGetSubGroup(item),
            subgroup,
            sizeof(subgroup));

        cfStringToCString(
            api.channelGetChannelName(item),
            name,
            sizeof(name));

        cfStringToCString(
            api.channelGetUnitLabel(item),
            unit,
            sizeof(unit));

        const bool ppmGroup =
            std::strcmp(
                group,
                "PPM Stats") == 0;

        const bool ppmNamedChannel =
            std::strstr(
                name,
                "BgtIdx") != nullptr;

        if (!ppmGroup &&
            !ppmNamedChannel)
            continue;

        const int32_t format =
            api.channelGetFormat(item);

        std::printf(
            "[%s] [%s] %s format=%d",
            group,
            subgroup,
            name,
            format);

        if (format == 1) {
            const int64_t value =
                api.simpleGetIntegerValue(
                    item,
                    0);

            std::printf(
                " value=%lld",
                static_cast<long long>(
                    value));

            if (unit[0] != '\0')
                std::printf(
                    " unit=%s",
                    unit);
        }

        else if (format == 2) {
            const int32_t stateCount =
                api.stateGetCount(item);

            std::printf(
                " states=%d",
                stateCount);

            for (int32_t state = 0;
                 state < stateCount;
                 ++state) {

                char stateName[128]{};

                cfStringToCString(
                    api.stateGetNameForIndex(
                        item,
                        state),
                    stateName,
                    sizeof(stateName));

                const int64_t residency =
                    api.stateGetResidency(
                        item,
                        state);

                if (stateName[0] != '\0') {
                    std::printf(
                        " {%s=%lld}",
                        stateName,
                        static_cast<long long>(
                            residency));
                }

                else {
                    std::printf(
                        " {state%d=%lld}",
                        state,
                        static_cast<long long>(
                            residency));
                }
            }
        }

        std::printf("\n");
    }

    CFRelease(delta);

    if (subscribed)
        CFRelease(subscribed);

    CFRelease(channels);
    dlclose(api.handle);

    return 0;
}

static int
ppmOpenScan()
{
    bool any = false;

    for (uint32_t type = 0;
         type < 16;
         ++type) {

        io_service_t service =
            IOServiceGetMatchingService(
                kIOMainPortDefault,
                IOServiceMatching(
                    "ApplePassthroughPPM"));

        if (!service) {
            std::fprintf(
                stderr,
                "ApplePassthroughPPM not found\n");
            return 1;
        }

        io_connect_t connect =
            IO_OBJECT_NULL;

        kern_return_t kr =
            IOServiceOpen(
                service,
                mach_task_self(),
                type,
                &connect);

        IOObjectRelease(service);

        std::printf(
            "type %2u: 0x%08x  %s%s\n",
            type,
            kr,
            mach_error_string(kr),
            kr == KERN_SUCCESS
                ? "  OPEN"
                : "");

        if (kr == KERN_SUCCESS) {
            any = true;

            if (connect != IO_OBJECT_NULL)
                IOServiceClose(connect);
        }
    }

    return any ? 0 : 1;
}

static int
ppmCpms()
{
    static constexpr uint32_t kSelector =
        0x1e;

    static constexpr size_t kOutputBytes =
        0x28c0;

    io_connect_t connect =
        openServiceConnection(
            "ApplePassthroughPPM",
            0);

    if (connect == IO_OBJECT_NULL)
        return 1;

    unsigned char output[kOutputBytes]{};

    size_t outputSize =
        sizeof(output);

    kern_return_t kr =
        IOConnectCallMethod(
            connect,
            kSelector,
            nullptr,
            0,
            nullptr,
            0,
            nullptr,
            nullptr,
            output,
            &outputSize);

    IOServiceClose(connect);

    if (kr != KERN_SUCCESS) {
        std::fprintf(
            stderr,
            "ppm-cpms selector=0x%x failed: %s (0x%x)\n",
            kSelector,
            mach_error_string(kr),
            kr);
        return 1;
    }

    std::printf(
        "ApplePassthroughPPM CPMS control state\n"
        "  selector     0x%x\n"
        "  output_size  0x%zx (%zu)\n"
        "  nonzero 16-byte rows:\n",
        kSelector,
        outputSize,
        outputSize);

    dumpNonZeroHex(
        output,
        outputSize);

    return 0;
}

static int
ppmClient(unsigned client)
{
    static constexpr uint32_t kSelector =
        0x1d;

    static constexpr size_t kOutputBytes =
        0x640;

    static constexpr size_t kCountOffset =
        0x1b8;

    static constexpr size_t kEntryOffset =
        0x1c0;

    static constexpr uint32_t kEntryMax =
        8;

    io_connect_t connect =
        openServiceConnection(
            "ApplePassthroughPPM",
            0);

    if (connect == IO_OBJECT_NULL)
        return 1;

    uint64_t inputScalars[1] = {
        client,
    };

    unsigned char output[kOutputBytes]{};

    size_t outputSize =
        sizeof(output);

    kern_return_t kr =
        IOConnectCallMethod(
            connect,
            kSelector,
            inputScalars,
            1,
            nullptr,
            0,
            nullptr,
            nullptr,
            output,
            &outputSize);

    IOServiceClose(connect);

    if (kr != KERN_SUCCESS) {
        std::fprintf(
            stderr,
            "ppm-client %u selector=0x%x failed: %s (0x%x)\n",
            client,
            kSelector,
            mach_error_string(kr),
            kr);
        return 1;
    }

    std::printf(
        "ApplePassthroughPPM client %u\n"
        "  selector     0x%x\n"
        "  output_size  0x%zx (%zu)\n",
        client,
        kSelector,
        outputSize,
        outputSize);

    if (outputSize >=
        kCountOffset + sizeof(uint32_t)) {

        uint32_t detailedCount = 0;

        std::memcpy(
            &detailedCount,
            output + kCountOffset,
            sizeof(detailedCount));

        std::printf(
            "  detailed_budget_count  %u\n",
            detailedCount);

        const uint32_t count =
            detailedCount < kEntryMax
                ? detailedCount
                : kEntryMax;

        for (uint32_t i = 0;
             i < count;
             ++i) {

            const size_t offset =
                kEntryOffset
                + static_cast<size_t>(i)
                * 16;

            if (offset + 16 > outputSize)
                break;

            uint32_t budget = 0;
            uint64_t details = 0;

            std::memcpy(
                &budget,
                output + offset + 4,
                sizeof(budget));

            std::memcpy(
                &details,
                output + offset + 8,
                sizeof(details));

            std::printf(
                "  detailed[%u] client=%u budget=%u details=0x%016llx\n",
                i,
                output[offset],
                budget,
                static_cast<unsigned long long>(
                    details));
        }
    }

    std::printf(
        "  nonzero 16-byte rows:\n");

    dumpNonZeroHex(
        output,
        outputSize);

    return 0;
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

    CFTypeRef property =
        IORegistryEntryCreateCFProperty(
            service,
            CFSTR("MBUProtocolVersion"),
            kCFAllocatorDefault,
            0);

    if (!property ||
        CFGetTypeID(property) !=
            CFNumberGetTypeID()) {

        if (property)
            CFRelease(property);

        std::fprintf(
            stderr,
            "loaded MBUnthrottleService does not advertise "
            "MBUProtocolVersion; the running kext is stale/older "
            "than this CLI. Re-stage/sign the current kext, approve it if "
            "prompted, reboot, then run kmutil load once after reboot.\n");

        IOObjectRelease(service);

        return IO_OBJECT_NULL;
    }

    int32_t kernelProtocol = 0;

    const Boolean gotProtocol =
        CFNumberGetValue(
            static_cast<CFNumberRef>(property),
            kCFNumberSInt32Type,
            &kernelProtocol);

    CFRelease(property);

    if (!gotProtocol ||
        kernelProtocol !=
            static_cast<int32_t>(
                kMBUProtocolVersion)) {

        std::fprintf(
            stderr,
            "protocol mismatch before IOServiceOpen: "
            "kernel=%d user=%u\n",
            kernelProtocol,
            kMBUProtocolVersion);

        IOObjectRelease(service);

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
printStatus(io_connect_t connect,
            uint32_t clusterMask)
{
    MBUStatusRequest request{};
    request.cluster_mask =
        clusterMask;

    MBUStatusReply reply{};

    size_t replySize =
        sizeof(reply);

    kern_return_t kr =
        IOConnectCallStructMethod(
            connect,
            kMBUSelectorGetStatus,
            &request,
            sizeof(request),
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

    std::printf(
        "metadata-only status; no MMIO was accessed\n");

    for (unsigned i = 0;
         i < reply.cluster_count &&
         i < kMBUClusterCount;
         ++i) {

        const auto &s =
            reply.clusters[i];

        if ((s.flags &
             kMBUClusterFlagSelected) == 0)
            continue;

        std::printf(
            "%s\n"
            "  base          0x%llx\n"
            "  cmd_phys      0x%llx\n"
            "  m1n1_default  %u\n",
            clusterName(i),
            static_cast<
                unsigned long long>(
                    s.cluster_base),
            static_cast<
                unsigned long long>(
                    s.command_phys),
            s.default_pstate);
    }

    return 0;
}

static int
readRegister(io_connect_t connect,
             unsigned cluster,
             unsigned reg)
{
    MBUReadRequest request{};
    request.cluster =
        static_cast<uint32_t>(cluster);
    request.reg =
        static_cast<uint32_t>(reg);

    MBUReadReply reply{};
    size_t replySize =
        sizeof(reply);

    kern_return_t kr =
        IOConnectCallStructMethod(
            connect,
            kMBUSelectorReadRegister,
            &request,
            sizeof(request),
            &reply,
            &replySize);

    if (kr != KERN_SUCCESS) {
        std::fprintf(
            stderr,
            "read failed: %s (0x%x)\n",
            mach_error_string(kr),
            kr);
        return 1;
    }

    if (reply.protocol_version
        != kMBUProtocolVersion) {

        std::fprintf(
            stderr,
            "protocol mismatch: kernel=%u user=%u\n",
            reply.protocol_version,
            kMBUProtocolVersion);
        return 1;
    }

    std::printf(
        "%s %s\n"
        "  phys   0x%llx\n"
        "  value  0x%016llx\n",
        clusterName(reply.cluster),
        registerName(reply.reg),
        static_cast<unsigned long long>(
            reply.physical_address),
        static_cast<unsigned long long>(
            reply.value));

    return 0;
}

static int
setPState(io_connect_t connect,
          unsigned cluster,
          unsigned pstate,
          bool verbose = true)
{
    MBUSetPStateRequest request{};
    request.cluster =
        static_cast<uint32_t>(cluster);
    request.pstate =
        static_cast<uint32_t>(pstate);

    MBUSetPStateReply reply{};
    size_t replySize =
        sizeof(reply);

    kern_return_t kr =
        IOConnectCallStructMethod(
            connect,
            kMBUSelectorSetPState,
            &request,
            sizeof(request),
            &reply,
            &replySize);

    if (kr != KERN_SUCCESS) {
        std::fprintf(
            stderr,
            "set-pstate failed: %s (0x%x)\n",
            mach_error_string(kr),
            kr);
        return 1;
    }

    if (reply.protocol_version
        != kMBUProtocolVersion) {

        std::fprintf(
            stderr,
            "protocol mismatch: kernel=%u user=%u\n",
            reply.protocol_version,
            kMBUProtocolVersion);
        return 1;
    }

    if (verbose) {
        std::printf(
            "%s pstate %u\n"
            "  phys       0x%llx\n"
            "  before     0x%016llx\n"
            "  submitted  0x%016llx\n"
            "  after      0x%016llx\n",
            clusterName(reply.cluster),
            reply.requested_pstate,
            static_cast<unsigned long long>(
                reply.physical_address),
            static_cast<unsigned long long>(
                reply.command_before),
            static_cast<unsigned long long>(
                reply.command_submitted),
            static_cast<unsigned long long>(
                reply.command_after));
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
        "  %s status <ECPU0|PCPU0|PCPU1> [cluster ...]\n"
        "  %s read <ECPU0|PCPU0|PCPU1> "
        "<cmd|last_change|status|pll_status|pll_factor>\n"
        "  %s set-pstate <ECPU0|PCPU0|PCPU1> <pstate>\n"
        "  %s hold-pstate <ECPU0|PCPU0|PCPU1> <pstate> [milliseconds]\n"
        "  %s ppm-setprops-probe\n"
        "  %s ppm-freshcell-status\n"
        "  %s ppm-freshcell <0|1>\n"
        "  %s ppm-baseline-status\n"
        "  %s ppm-baseline <0|1>\n"
        "  %s ppm-syscap-status\n"
        "  %s ppm-syscap <mW>\n"
        "  %s ppm-syscap-clear\n"
        "  %s ppm-ioreport [milliseconds]\n"
        "  %s ppm-open-scan\n"
        "  %s ppm-cpms\n"
        "  %s ppm-client <client-id>\n"
        "  %s restore-default\n"
        "  %s hold-default [milliseconds]\n"
        "\n"
        "ppm-cpms and ppm-client are read-only ApplePassthroughPPM "
        "queries and do not require MBUnthrottle.kext.\n"
        "status is metadata-only. read performs exactly one "
        "64-bit MMIO load. set-pstate performs one explicit "
        "read-modify-write transition on the selected cluster.\n",
        argv0,
        argv0,
        argv0,
        argv0,
        argv0,
        argv0,
        argv0,
        argv0,
        argv0,
        argv0,
        argv0,
        argv0,
        argv0,
        argv0,
        argv0,
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

    if (std::strcmp(
            argv[1],
            "ppm-ioreport") == 0) {

        if (argc < 2 ||
            argc > 3) {

            usage(argv[0]);
            return 2;
        }

        unsigned intervalMs =
            1000;

        if (argc == 3) {
            char *end = nullptr;

            const unsigned long parsed =
                std::strtoul(
                    argv[2],
                    &end,
                    0);

            if (!end ||
                *end != '\0' ||
                parsed < 10 ||
                parsed > 60000) {

                std::fprintf(
                    stderr,
                    "IOReport interval must be 10..60000 ms\n");

                return 2;
            }

            intervalMs =
                static_cast<unsigned>(
                    parsed);
        }

        return ppmIOReport(
            intervalMs);
    }

    if (std::strcmp(
            argv[1],
            "ppm-freshcell-status") == 0) {

        if (argc != 2) {
            usage(argv[0]);
            return 2;
        }

        return ppmFreshCellStatus();
    }

    if (std::strcmp(
            argv[1],
            "ppm-freshcell") == 0) {

        if (argc != 3) {
            usage(argv[0]);
            return 2;
        }

        char *end = nullptr;

        const unsigned long parsed =
            std::strtoul(
                argv[2],
                &end,
                0);

        if (!end ||
            *end != '\0' ||
            parsed > 1) {

            std::fprintf(
                stderr,
                "fresh-cell value must be 0 or 1\n");

            return 2;
        }

        return ppmSetFreshCell(
            static_cast<uint32_t>(
                parsed));
    }

    if (std::strcmp(
            argv[1],
            "ppm-baseline-status") == 0) {

        if (argc != 2) {
            usage(argv[0]);
            return 2;
        }

        return ppmBaselineStatus();
    }

    if (std::strcmp(
            argv[1],
            "ppm-baseline") == 0) {

        if (argc != 3) {
            usage(argv[0]);
            return 2;
        }

        char *end = nullptr;

        const unsigned long parsed =
            std::strtoul(
                argv[2],
                &end,
                0);

        if (!end ||
            *end != '\0' ||
            parsed > 1) {

            std::fprintf(
                stderr,
                "baseline value must be 0 or 1\n");

            return 2;
        }

        return ppmSetBaseline(
            static_cast<uint32_t>(
                parsed));
    }

    if (std::strcmp(
            argv[1],
            "ppm-syscap-status") == 0) {

        if (argc != 2) {
            usage(argv[0]);
            return 2;
        }

        return ppmSyscapStatus();
    }

    if (std::strcmp(
            argv[1],
            "ppm-syscap") == 0) {

        if (argc != 3) {
            usage(argv[0]);
            return 2;
        }

        char *end = nullptr;

        const unsigned long parsed =
            std::strtoul(
                argv[2],
                &end,
                0);

        if (!end ||
            *end != '\0' ||
            parsed < 1000 ||
            parsed > 100000) {

            std::fprintf(
                stderr,
                "system capability must be 1000..100000 mW\n");

            return 2;
        }

        return ppmSetSyscap(
            static_cast<uint32_t>(
                parsed));
    }

    if (std::strcmp(
            argv[1],
            "ppm-syscap-clear") == 0) {

        if (argc != 2) {
            usage(argv[0]);
            return 2;
        }

        return ppmClearSyscap();
    }

    if (std::strcmp(
            argv[1],
            "ppm-setprops-probe") == 0) {

        if (argc != 2) {
            usage(argv[0]);
            return 2;
        }

        return ppmSetPropertiesProbe();
    }

    if (std::strcmp(
            argv[1],
            "ppm-open-scan") == 0) {

        if (argc != 2) {
            usage(argv[0]);
            return 2;
        }

        return ppmOpenScan();
    }

    if (std::strcmp(
            argv[1],
            "ppm-cpms") == 0) {

        if (argc != 2) {
            usage(argv[0]);
            return 2;
        }

        return ppmCpms();
    }

    if (std::strcmp(
            argv[1],
            "ppm-client") == 0) {

        if (argc != 3) {
            usage(argv[0]);
            return 2;
        }

        char *end = nullptr;

        const unsigned long parsed =
            std::strtoul(
                argv[2],
                &end,
                0);

        if (!end ||
            *end != '\0' ||
            parsed > 255) {

            std::fprintf(
                stderr,
                "client-id must be 0..255\n");

            return 2;
        }

        return ppmClient(
            static_cast<unsigned>(
                parsed));
    }

    io_connect_t connect =
        openConnection();

    if (connect == IO_OBJECT_NULL)
        return 1;

    int result = 0;

    if (std::strcmp(
            argv[1],
            "status") == 0) {

        uint32_t clusterMask = 0;

        if (!parseStatusMask(
                argc,
                argv,
                &clusterMask)) {

            usage(argv[0]);
            result = 2;
        } else {
            result =
                printStatus(
                    connect,
                    clusterMask);
        }
    }

    else if (std::strcmp(
                 argv[1],
                 "read") == 0) {

        if (argc != 4) {
            usage(argv[0]);
            result = 2;
        } else {
            const int cluster =
                clusterIndex(argv[2]);

            const int reg =
                registerIndex(argv[3]);

            if (cluster < 0 || reg < 0) {
                usage(argv[0]);
                result = 2;
            } else {
                if (cluster ==
                    kMBUClusterPCPU1) {
                    std::fprintf(
                        stderr,
                        "warning: PCPU1 cmd previously caused an LLC "
                        "Bus Error while unavailable; this command will "
                        "perform the requested access exactly.\n");
                }

                result =
                    readRegister(
                        connect,
                        static_cast<unsigned>(
                            cluster),
                        static_cast<unsigned>(
                            reg));
            }
        }
    }

    else if (std::strcmp(
                 argv[1],
                 "set-pstate") == 0) {

        if (argc != 4) {
            usage(argv[0]);
            result = 2;
        } else {
            const int cluster =
                clusterIndex(argv[2]);

            char *end = nullptr;
            const long parsed =
                std::strtol(
                    argv[3],
                    &end,
                    10);

            if (cluster < 0 ||
                !end ||
                *end != '\0' ||
                parsed < 1 ||
                parsed > 31) {

                usage(argv[0]);
                result = 2;
            } else {
                const unsigned maxPState =
                    cluster ==
                        kMBUClusterECPU0
                        ? 7U
                        : 17U;

                if (static_cast<unsigned>(
                        parsed) > maxPState) {

                    std::fprintf(
                        stderr,
                        "pstate out of known T6020 range: "
                        "%s accepts 1..%u\n",
                        clusterName(
                            static_cast<unsigned>(
                                cluster)),
                        maxPState);

                    result = 2;
                } else {
                    if (cluster !=
                        kMBUClusterECPU0) {

                        std::fprintf(
                            stderr,
                            "warning: P-cluster MMIO can panic if "
                            "that cluster is power-gated; proceeding "
                            "because you explicitly selected it.\n");
                    }

                    result =
                        setPState(
                            connect,
                            static_cast<unsigned>(
                                cluster),
                            static_cast<unsigned>(
                                parsed));
                }
            }
        }
    }

    else if (std::strcmp(
                 argv[1],
                 "hold-pstate") == 0) {

        if (argc < 4 || argc > 5) {
            usage(argv[0]);
            result = 2;
        } else {
            const int cluster =
                clusterIndex(argv[2]);

            char *end = nullptr;
            const long parsedPState =
                std::strtol(
                    argv[3],
                    &end,
                    10);

            if (cluster < 0 ||
                !end ||
                *end != '\0' ||
                parsedPState < 1 ||
                parsedPState > 31) {

                usage(argv[0]);
                result = 2;
            } else {
                const unsigned maxPState =
                    cluster ==
                        kMBUClusterECPU0
                        ? 7U
                        : 17U;

                if (static_cast<unsigned>(
                        parsedPState) > maxPState) {

                    std::fprintf(
                        stderr,
                        "pstate out of known T6020 range: "
                        "%s accepts 1..%u\n",
                        clusterName(
                            static_cast<unsigned>(
                                cluster)),
                        maxPState);

                    result = 2;
                } else {
                    unsigned intervalMs = 10;

                    if (argc == 5) {
                        char *intervalEnd = nullptr;
                        const long parsedInterval =
                            std::strtol(
                                argv[4],
                                &intervalEnd,
                                10);

                        if (!intervalEnd ||
                            *intervalEnd != '\0' ||
                            parsedInterval < 1 ||
                            parsedInterval > 5000) {

                            std::fprintf(
                                stderr,
                                "interval must be 1..5000 ms\n");
                            IOServiceClose(connect);
                            return 2;
                        }

                        intervalMs =
                            static_cast<unsigned>(
                                parsedInterval);
                    }

                    if (cluster !=
                        kMBUClusterECPU0) {

                        std::fprintf(
                            stderr,
                            "warning: repeated P-cluster MMIO can panic "
                            "if that cluster power-gates between writes; "
                            "proceeding because you explicitly selected it.\n");
                    }

                    std::printf(
                        "holding %s at pstate %ld every %u ms\n"
                        "Ctrl-C to stop\n",
                        clusterName(
                            static_cast<unsigned>(
                                cluster)),
                        parsedPState,
                        intervalMs);

                    for (;;) {
                        if (setPState(
                                connect,
                                static_cast<unsigned>(
                                    cluster),
                                static_cast<unsigned>(
                                    parsedPState),
                                false) != 0) {

                            result = 1;
                            break;
                        }

                        usleep(
                            intervalMs * 1000);
                    }
                }
            }
        }
    }

    else if (std::strcmp(
                 argv[1],
                 "restore-default") == 0) {

        result =
            restoreDefaults(connect);

        if (result == 0)
            result =
                printStatus(
                    connect,
                    kMBUClusterMaskECPU0 |
                    kMBUClusterMaskPCPU0);
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
