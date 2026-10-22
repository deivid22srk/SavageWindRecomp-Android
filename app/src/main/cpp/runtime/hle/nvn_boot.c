// NVN entry points: every NVN function is looked up by name and bound to a host stub.
#include <stdlib.h>
#include <string.h>

#include "../hle.h"

// (debug) SAVAGEWIND_NVN_REAL=1: also asks the game's own NVN driver (the sdk's nvnBootstrapLoader, recompiled
// like the rest) where each function lives and logs it. The real implementations, read in Ghidra, show the NVN
// version's struct sizes (what SetDefaults clears, what Initialize reads), which the HLE layouts must fit in.
static void log_real_proc(Cpu *c, const char *name) {
    static int on = -1;
    static uint64_t real_gpa;
    if (on < 0) on = getenv("SAVAGEWIND_NVN_REAL") != NULL;
    if (!on || !name) return;
    Cpu saved = *c;
    if (!real_gpa) {
        uint64_t boot = loader_find_symbol_in("sdk", "nvnBootstrapLoader");
        if (!boot) { on = 0; LOG("[nvn-real] sdk exports no nvnBootstrapLoader\n"); return; }
        c->x[0] = (uint64_t)(uintptr_t)"nvnDeviceGetProcAddress";
        guest_call(c, boot);
        real_gpa = c->x[0];
        LOG("[nvn-real] bootstrap %llx -> nvnDeviceGetProcAddress %llx\n", (unsigned long long)boot,
            (unsigned long long)real_gpa);
        *c = saved;
        if (!real_gpa) { on = 0; return; }
    }
    c->x[0] = 0;  // (no device: the lookup the game itself does first, FUN_710000eb00(0, ...))
    c->x[1] = (uint64_t)(uintptr_t)name;
    guest_call(c, real_gpa);
    LOG("[nvn-real] %s %llx\n", name, (unsigned long long)c->x[0]);
    *c = saved;
}

static uint64_t nvn_proc(const char *name) {
    if (!name || !*name) return 0;
    static int log = -1;  // SAVAGEWIND_NVN_LOG: every function the game looks up
    if (log < 0) log = getenv("SAVAGEWIND_NVN_LOG") != NULL;
    HostFn fn = hle_lookup_impl(name);
    if (log) LOG("[nvn] proc %s%s\n", name, fn ? "" : " (not implemented)");
    return hle_register(name, fn);
}

HLE_FUNC("nvnBootstrapLoader", nvn_bootstrap_loader) {
    const char *name = (const char *)(uintptr_t)c->x[0];
    c->x[0] = nvn_proc(name);
}

// void* nvnDeviceGetProcAddress(const NVNdevice*, const char*)
HLE_FUNC("nvnDeviceGetProcAddress", nvn_device_get_proc_address) {
    const char *name = (const char *)(uintptr_t)c->x[1];
    log_real_proc(c, name);
    c->x[0] = nvn_proc(name);
}
