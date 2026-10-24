// Runtime-internal declarations.
#pragma once
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>

#include "cpu.h"
#include "platform.h"

#define LOG(...) fprintf(stderr, __VA_ARGS__)
#define FATAL(...) (fprintf(stderr, "FATAL: " __VA_ARGS__), rt_fatal_trace())

// ---- address space layout (guest addresses == host addresses)
// The guest lives in a 12.5 GB window claimed at startup (see main.c); every region below sits
// inside it. The base is per-platform: macOS never maps the 8 GB..448 GB range, so there the
// window must start at 0x7000000000 - but on Android that exact address is taken: the zygote's
// ART boot image is mapped at a fixed low address on 39-bit-VA devices and every app inherits
// it (dlopen'ing libmain used to fail its reservation and silently exit(1) - the restart loop).
// Android/Linux therefore claim a window at 0x3000000000 (192 GB), far below everything the
// system maps (system libs live at the very top, the boot image at 448 GB) on both 39- and
// 48-bit-VA devices. Windows has no such constraint and keeps the original base.
#ifdef __APPLE__
#define GUEST_LO     0x7000000000ull
#elif defined(_WIN32)
#define GUEST_LO     0x7000000000ull
#else
#define GUEST_LO     0x3000000000ull
#endif
#define GUEST_HI     (GUEST_LO + 0x320000000ull)  // +12.5 GB
// The exefs modules (main, subsdk0..9, sdk) sit back to back from MAIN_BASE, main first, as tools/layout.py
// places them; the game code library says where each one is (runtime/gamelib.h). Both bounds: tools/layout.py too.
#define MAIN_BASE    GUEST_LO
#define MODULES_END  (GUEST_LO + 0x080000000ull)
#define HLE_BASE     (GUEST_LO + 0x080000000ull)  // fake addresses for host-implemented functions
#define HLE_SIZE     0x0000100000ull
#define TLS_BASE     (GUEST_LO + 0x090000000ull)
#define TLS_SIZE     0x0000100000ull
#define HLE_HEAP     (GUEST_LO + 0x092000000ull)  // guest-visible memory owned by host code
#define HLE_HEAP_SIZE 0x000E000000ull
#define ALIAS_BASE   (GUEST_LO + 0x100000000ull)  // alias region (svcMapMemory targets)
#define ALIAS_SIZE   0x0100000000ull
#define HEAP_BASE    (GUEST_LO + 0x240000000ull)
#define HEAP_MAX     0x00D0000000ull  // 3.25 GB
// Guest-ish pointer range checks that predate GUEST_HI (fnprof.c's vtable peek, cmdbuf.c's
// frame-pointer filter): the same width as before, relative to the base.
#define GUEST_WIDE_HI (GUEST_LO + 0x1000000000ull)

// ---- loader.c
typedef struct Module {
    const char *name;
    uint64_t base, size;
    uint64_t text_off, text_size, ro_off, ro_size, data_off, data_size;
    uint64_t dynamic, init, fini, init_array, init_array_sz, fini_array, fini_array_sz;
    uint64_t symtab, strtab, nsyms;
} Module;

#define MAX_MODULES 12  // main, subsdk0..9, sdk

extern Module g_modules[MAX_MODULES];  // in load order
extern int g_nmodules;
void loader_load(const char *exefs_dir);  // (again after a reboot: modules are reloaded into wiped memory)
Module *loader_module(const char *name);  // NULL if the game has no such module
uint64_t loader_find_symbol(const char *name);
uint64_t loader_find_symbol_in(const char *module, const char *name);  // an export of that module only
const char *loader_symbol_at(uint64_t addr, uint64_t *offset);

// ---- dispatch.c
typedef void (*HostFn)(Cpu *c);
void dispatch_init(void);
GuestFn dispatch_lookup(uint64_t addr);
uint64_t hle_register(const char *name, HostFn fn);  // returns fake guest address
HostFn hle_lookup_impl(const char *name);
void hle_override(uint64_t guest_addr, HostFn fn, const char *name);
void rt_fatal_trace(void) __attribute__((noreturn));
void crash_handler_install(void);  // per thread (alternate signal stack)

// ---- kernel.c
typedef struct KThread KThread;
void kernel_init(void);
void kernel_stop_threads(void (*idle)(void));  // reboot: unwind every guest thread and join it (idle: called while waiting)
void kernel_reset(void);                       // reboot: empty handle table, memory map, thread list
KThread *kernel_create_main_thread(uint64_t entry, uint64_t stack_size, int prio);
void kernel_run_main(KThread *t, uint64_t x0, uint64_t x1, uint64_t x2, uint64_t x3, uint64_t x4);
uint32_t kernel_current_thread_handle(void);
Cpu *kernel_current_cpu(void);

// ---- ipc.c
typedef struct Service Service;
int ipc_handle_request(Cpu *c, uint32_t handle);
uint32_t ipc_connect_named_port(const char *name);

// ---- in-place reboot (main.c): boot again or go back to the start screen without leaving the process
enum { RT_REBOOT_NONE, RT_REBOOT_GAME, RT_REBOOT_TITLE };  // GAME: boot again at once; TITLE: start screen
void rt_request_reboot(int kind);  // any thread; the window loop (ui_run) returns it and main() carries it out
int rt_reboot_requested(void);
extern volatile int g_rt_unwind;   // set while guest threads are being stopped (kernel.c)
void rt_unwind_point(void);        // guest threads: unwind to thread_main if a reboot is stopping them

// ---- ui (runtime/nvn/metal.m)
int ui_run(void);  // window loop; returns the reboot kind requested (the window stays open)

// ---- helpers
void guest_call(Cpu *c, uint64_t addr);   // call guest function at addr (saves/restores x30)
uint64_t guest_sym(const char *name);     // cached loader_find_symbol, fatal if missing
void *hle_alloc(uint64_t size);           // zeroed guest-visible memory (never freed)
extern const char *g_romfs_dir;
extern uint64_t g_program_id;     // from main.npdm's ACI0 (runtime/main.c); 0 before boot
