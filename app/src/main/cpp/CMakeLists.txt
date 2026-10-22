# Savage Wind Recomp - Android port (arm64-v8a)
# Builds libmain.so: the original C runtime (runtime/, unchanged except for the Android platform
# layer and #ifdef'd process handling) + the Vulkan backend + ImGui + the JNI/TCC bridge.
# SDL3, FFmpeg, shaderc, Vulkan headers and VMA come prebuilt from scripts/build_deps_android.sh
# into deps/ (see the script; the CI workflow runs it before Gradle).
cmake_minimum_required(VERSION 3.22)
project(savagewind C CXX)

set(RUNTIME ${CMAKE_CURRENT_SOURCE_DIR}/runtime)
set(TOOLS ${CMAKE_CURRENT_SOURCE_DIR}/tools)
set(ANDROID_DIR ${CMAKE_CURRENT_SOURCE_DIR}/android)
set(IMGUI ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
set(STB ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb)
set(SMAA ${CMAKE_CURRENT_SOURCE_DIR}/third_party/smaa)
set(DEPS ${CMAKE_CURRENT_SOURCE_DIR}/deps)
set(PY ${CMAKE_CURRENT_SOURCE_DIR}/../python)

include_directories(${RUNTIME} ${RUNTIME}/nvn ${STB} ${IMGUI} ${IMGUI}/backends ${DEPS}/include)

# ------------------------------------------------------------------ GAMELIB_ID
# The same hash runtime/gamebuild.c and the generated library share (the Makefile computes it
# from the generator sources, the headers they compile against, and the flags).
set(GAMELIB_ID_FILES
    ${PY}/gen.py ${PY}/lifter.py ${PY}/analyze.py ${PY}/nso.py
    ${PY}/passes.py ${PY}/patches.py ${PY}/simd_c.py
    ${TOOLS}/gamelib_entry.c
    ${RUNTIME}/hle/hle_symbols.txt
    ${RUNTIME}/cpu.h ${RUNTIME}/simd.h ${RUNTIME}/simd_fp.h ${RUNTIME}/gamelib.h
    ${PY}/layout.py)
set(GENFLAGS_ID "-O1 -fno-strict-aliasing -fvisibility=hidden -Iruntime")
set(GAMELIB_ID_INPUT "${CMAKE_BINARY_DIR}/gamelib_id_input")
file(WRITE "${GAMELIB_ID_INPUT}" "")
foreach(F ${GAMELIB_ID_FILES})
    file(READ "${F}" CONTENT)
    file(APPEND "${GAMELIB_ID_INPUT}" "${CONTENT}")
endforeach()
file(APPEND "${GAMELIB_ID_INPUT}" "${GENFLAGS_ID}\n")
file(SHA256 "${GAMELIB_ID_INPUT}" GAMELIB_ID_HASH)
string(SUBSTRING "${GAMELIB_ID_HASH}" 0 16 GAMELIB_ID)
message(STATUS "GAMELIB_ID: ${GAMELIB_ID}")

# ------------------------------------------------------------------ sources
file(GLOB RT_SRC ${RUNTIME}/*.c ${RUNTIME}/hle/*.c ${RUNTIME}/services/*.c
                 ${RUNTIME}/nvn/*.c ${RUNTIME}/nvn/maxwell/*.c)
file(GLOB VK_SRC ${RUNTIME}/nvn/vulkan/*.c ${RUNTIME}/nvn/vulkan/*.cpp)
file(GLOB UI_SRC ${RUNTIME}/ui/*.mm)
file(GLOB IMGUI_CORE ${IMGUI}/imgui.cpp ${IMGUI}/imgui_draw.cpp ${IMGUI}/imgui_tables.cpp
                     ${IMGUI}/imgui_widgets.cpp)
set(IMGUI_BACKENDS ${IMGUI}/backends/imgui_impl_sdl3.cpp ${IMGUI}/backends/imgui_impl_vulkan.cpp)

add_library(main SHARED
    ${RT_SRC} ${VK_SRC} ${UI_SRC}
    ${RUNTIME}/platform/android.c
    ${RUNTIME}/nvn/smaa_data.c
    ${RUNTIME}/nvn/vulkan/vma.cpp
    ${IMGUI_CORE} ${IMGUI_BACKENDS}
    ${TOOLS}/gamelib_entry.c
    ${ANDROID_DIR}/android_main.c ${ANDROID_DIR}/android_bridge.c)

# the .mm UI sources are plain C++ apart from __APPLE__ blocks (as on Linux/Windows)
set_source_files_properties(${UI_SRC} PROPERTIES LANGUAGE CXX)

set_target_properties(main PROPERTIES
    OUTPUT_NAME "main"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")

target_compile_definitions(main PRIVATE
    GAMELIB_ID="${GAMELIB_ID}")
# gamebuild.c / gamelib.c carry the game-code build's flags (as the Makefile's -D does)
set_source_files_properties(${RUNTIME}/gamebuild.c ${RUNTIME}/gamelib.c PROPERTIES
    COMPILE_DEFINITIONS "GAMELIB_CFLAGS=\"-O1 -fno-strict-aliasing\";GAMELIB_SHARED=\"-shared\"")

# the SMAA reference shader textures live next to the embedded shader string (as the Makefile's -I)
target_include_directories(main PRIVATE ${SMAA}/Textures)

# ------------------------------------------------------------------ flags
target_compile_options(main PRIVATE
    -O2 -g -fno-strict-aliasing -Wall -Wno-unused-function -Wno-format
    -Wno-return-type -Wno-unused-label)

# ------------------------------------------------------------------ link
find_library(android-lib android)
find_library(log-lib log)
find_library(z-lib z)

# shaderc's static build is several archives (shaderc, shaderc_util, glslang, SPIRV-Tools...):
# a --start-group lets lld resolve them in any order.
file(GLOB SHADERC_ARCHIVES ${DEPS}/lib/shaderc/*.a)
list(SORT SHADERC_ARCHIVES)

target_link_libraries(main PRIVATE
    ${DEPS}/lib/libSDL3.so
    -Wl,--start-group
    ${DEPS}/lib/libavformat.a ${DEPS}/lib/libavcodec.a ${DEPS}/lib/libavutil.a
    ${DEPS}/lib/libswscale.a
    -Wl,--end-group
    ${SHADERC_ARCHIVES}
    vulkan
    ${android-lib} ${log-lib} ${z-lib})
