/* SPDX-License-Identifier: GPL-2.0-only
 * XNU OSKext::start/stop owns C++ constructors on x86_64; module callbacks stay empty. */
#include <mach/mach_types.h>
#include <mach/kmod.h>
extern kern_return_t _start(kmod_info_t *, void *);
extern kern_return_t _stop(kmod_info_t *, void *);
KMOD_EXPLICIT_DECL(io.github.kodeaqua.RTL8188EUProbe, "0.1.0", _start, _stop)
static kern_return_t probe_start(kmod_info_t *ki, void *d) { (void)ki; (void)d; return KERN_SUCCESS; }
static kern_return_t probe_stop(kmod_info_t *ki, void *d)  { (void)ki; (void)d; return KERN_SUCCESS; }
kmod_start_func_t *_realmain = probe_start;
kmod_stop_func_t *_antimain = probe_stop;
