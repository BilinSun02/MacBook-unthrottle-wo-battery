// SPDX-License-Identifier: GPL-2.0-only

#include <mach/mach_types.h>
#include <mach/kmod.h>

/*
 * _start/_stop come from the kmod C++ runtime.
 *
 * For an IOKit C++ driver we do NOT supply our own module-level
 * start/stop functions. IOService::start()/stop() are called later
 * by IOKit when the personality matches.
 */
extern kern_return_t _start(kmod_info_t *ki, void *data);
extern kern_return_t _stop(kmod_info_t *ki, void *data);

/*
 * Produces the global 'kmod_info' object.
 *
 * Mach-O prepends '_' to C external symbols, so kmutil will see
 * this as '_kmod_info'.
 */
KMOD_EXPLICIT_DECL(
    dev.experimental.MBUnthrottle,
    "0.1.0",
    _start,
    _stop
)

/*
 * IOKit driver: there is no separate programmer-supplied kmod
 * entry point. The C++ runtime startup initializes OSMetaClass
 * registrations, after which IOKit creates MBUnthrottleService
 * from the personality in Info.plist.
 */
__private_extern__ kmod_start_func_t *_realmain = 0;
__private_extern__ kmod_stop_func_t  *_antimain = 0;

/*
 * Expected by the historical kext runtime/generated-info contract.
 */
__private_extern__ int _kext_apple_cc = __APPLE_CC__;
