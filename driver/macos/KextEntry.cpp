// SPDX-License-Identifier: GPL-2.0-only
#include <mach/kmod.h>
extern "C" {
extern kern_return_t _start(kmod_info_t*,void*);
extern kern_return_t _stop(kmod_info_t*,void*);
KMOD_EXPLICIT_DECL(local.realtekvtdnext.ethernet,"0.21.0",_start,_stop)
__attribute__((visibility("hidden"))) kmod_start_func_t *_realmain=nullptr;
__attribute__((visibility("hidden"))) kmod_stop_func_t *_antimain=nullptr;
__attribute__((visibility("hidden"))) int _kext_apple_cc=__APPLE_CC__;
}
