// The recompiled game code: a library built from the player's own game files (`make gen` + `make` in a source
// checkout, the start screen's setup otherwise; runtime/gamebuild.c) and loaded at startup (runtime/gamelib.c).
// Nothing from the game ships with the app.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cpu.h"

#if defined(_WIN32)
#define GAMELIB_NAME "swgame.dll"
#define GAMELIB_EXPORT __declspec(dllexport)
#elif defined(__APPLE__)
#define GAMELIB_NAME "swgame.dylib"
#define GAMELIB_EXPORT __attribute__((visibility("default")))
#else
#define GAMELIB_NAME "swgame.so"
#define GAMELIB_EXPORT __attribute__((visibility("default")))
#endif

// GAMELIB_ID (-D, from the Makefile): a hash of the generator and the headers the game code is compiled against. A
// library built with another one is rebuilt, not loaded.
#ifndef GAMELIB_ID
#define GAMELIB_ID "dev"
#endif
#define GAMELIB_ABI 2

// One exefs module as the game code was generated for it: where it goes (tools/layout.py, the generated layout.c)
// and its functions. runtime/loader.c loads the module there.
typedef struct {
    const char *name;        // its exefs file: main, subsdk0..9, sdk
    uint64_t base, size;     // guest address, image size (page-rounded)
    const char *build_id;    // NSO build id (64 hex digits) of the file the code was generated from
    const uint64_t *addrs;   // sorted guest addresses
    const GuestFn *fns;      // the functions at them
    const uint64_t *count;
} GameLibModule;

typedef struct {
    uint32_t abi;    // GAMELIB_ABI
    const char *id;  // GAMELIB_ID
    const GameLibModule *modules;  // in load order, main first
    const uint32_t *nmodules;
} GameLibTables;

// The library's one exported function (tools/gamelib_entry.c).
typedef const GameLibTables *(*GameLibEntry)(void);
#define GAMELIB_ENTRY "swgame_tables"

#ifdef __cplusplus
extern "C" {
#endif

// Loads the game code for the game files in game_dir (once; later calls return the first result). Looks at
// SAVAGEWIND_GAME_LIB, next to the executable (a source checkout's build/), then game_dir. false: why says what's wrong.
bool gamelib_load(const char *game_dir, char *why, size_t why_size);
// The tables of the loaded library (NULL before gamelib_load succeeds).
const GameLibTables *gamelib_tables(void);
// The loaded library's main module's build id (NULL before a successful load; main is first in the load order).
// The first 16 hex digits say the game version: "de0b6ac4efc1dda7" is 1.8.2, "2094f152836683a3" is 1.0.0. What
// the runtime only knows at 1.8.2's addresses (runtime/hle/framepace.c) gates on this.
const char *gamelib_main_build_id(void);
// True when a library usable with this build is in one of those places (without loading it).
bool gamelib_present(const char *game_dir);

#ifdef __cplusplus
}
#endif
