// The app's entry point: SDL3's SDLActivity dlsym's SDL_main from libmain.so and calls it on
// its SDL thread. Everything else is the original runtime (runtime/main.c, renamed to sw_main
// on Android).
#include <SDL3/SDL.h>

int sw_main(int argc, char **argv);

int SDL_main(int argc, char **argv) {
    return sw_main(argc, argv);
}
