// SPDX-License-Identifier: GPL-2.0-only

#include <mach/mach_types.h>
#include <mach/kmod.h>

/*
 * _start/_stop are supplied by the C++ kext runtime (libkmodc++).
 * IOService::start()/stop() are invoked later by IOKit when the
 * personality matches and the service instance is created.
 */
extern kern_return_t _start(kmod_info_t *ki, void *data);
extern kern_return_t _stop(kmod_info_t *ki, void *data);

/*
 * Emit the global kmod_info object expected by kmutil/kext validation.
 * Mach-O external C symbols are displayed with a leading underscore,
 * so this appears as "_kmod_info" in nm output.
 */
KMOD_EXPLICIT_DECL(
    dev.experimental.MBUnthrottle,
    "0.1.4",
    _start,
    _stop
)

/*
 * Historical C++ kext runtime bookkeeping. There is no separate
 * programmer-provided kmod start/stop routine for this IOService driver.
 */
__private_extern__ kmod_start_func_t *_realmain = 0;
__private_extern__ kmod_stop_func_t  *_antimain = 0;
__private_extern__ int _kext_apple_cc = __APPLE_CC__;
