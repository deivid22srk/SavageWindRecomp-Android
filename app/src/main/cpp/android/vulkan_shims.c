// Vulkan 1.2/1.3 core entry points the runtime calls directly. Android's Vulkan loader only
// exports 1.0/1.1 symbols: anything newer must come from vkGetDeviceProcAddr. Rather than change
// the vendored backend (runtime/nvn/vulkan), these shims carry the same C signatures and forward
// to the real driver functions, learned when the runtime creates its device (the only one).
//
// Custom Vulkan drivers (AdrenoTools / Turnip): when the user picked a driver in the start
// screen's panel (MainActivity exports SAVAGEWIND_VK_DRIVER_DIR / SAVAGEWIND_VK_DRIVER_LIB), the
// first instance-level call opens libvulkan.so through libadrenotools, which hooks the loader's
// driver enumeration into loading the custom driver instead. The handle that call returns
// supplies every instance-level entry point (the documented AdrenoTools contract); without a
// custom driver, or if anything in that path fails, everything falls back to the system loader
// exactly as before. Nothing here can take the app down: any failure is logged and the system
// driver is used.
#include <dlfcn.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include <vulkan/vulkan.h>

#if defined(__ANDROID__) && defined(__aarch64__) && defined(HAVE_ADRENOTOOLS)
#define SW_ADRENOTOOLS 1
#include <android/log.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <adrenotools/driver.h>
#endif

#if defined(SW_ADRENOTOOLS)
#define SW_LOG_TAG "swboot"

// Append to the boot log from here too: the driver decision happens long before the renderer
// exists, and it must be visible in the same file a crash report would carry.
static void sw_log(const char *what) {
    __android_log_print(ANDROID_LOG_INFO, SW_LOG_TAG, "%s", what);
    const char *p = getenv("SAVAGEWIND_CRASHLOG");
    if (p) {
        int fd = open(p, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
        if (fd >= 0) {
            ssize_t w = write(fd, what, strlen(what));
            (void)w;
            ssize_t w2 = write(fd, "\n", 1);
            (void)w2;
            close(fd);
        }
    }
}

// Is the system driver Qualcomm's (an Adreno)? Custom Adreno drivers are only meaningful there.
// Runs against a plain dlopen'ed loader, before any hook exists.
static bool sw_probe_is_adreno(char *name, size_t name_n) {
    void *lib = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    if (!lib) return false;
    bool adreno = false;
    PFN_vkCreateInstance create = (PFN_vkCreateInstance)dlsym(lib, "vkCreateInstance");
    PFN_vkGetInstanceProcAddr gipa = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr");
    PFN_vkEnumeratePhysicalDevices enum_dev = (PFN_vkEnumeratePhysicalDevices)dlsym(lib, "vkEnumeratePhysicalDevices");
    if (create && gipa && enum_dev) {
        VkInstance inst;
        VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_1};
        VkInstanceCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
        if (create(&ci, NULL, &inst) == VK_SUCCESS) {
            uint32_t n = 0;
            if (enum_dev(inst, &n, NULL) == VK_SUCCESS && n) {
                VkPhysicalDevice *pd = (VkPhysicalDevice *)calloc(n, sizeof *pd);
                if (pd && enum_dev(inst, &n, pd) == VK_SUCCESS) {
                    PFN_vkGetPhysicalDeviceProperties props =
                        (PFN_vkGetPhysicalDeviceProperties)gipa(inst, "vkGetPhysicalDeviceProperties");
                    for (uint32_t i = 0; props && i < n; i++) {
                        VkPhysicalDeviceProperties p;
                        props(pd[i], &p);
                        if (p.vendorID == 0x5143) {  // Qualcomm
                            adreno = true;
                            if (name && name_n) snprintf(name, name_n, "%s", p.deviceName);
                            break;
                        }
                    }
                }
                free(pd);
            }
            PFN_vkDestroyInstance destroy = (PFN_vkDestroyInstance)gipa(inst, "vkDestroyInstance");
            if (destroy) destroy(inst, NULL);
        }
    }
    dlclose(lib);
    return adreno;
}

static void *g_adreno_libvulkan;  // the hooked libvulkan.so handle (custom driver active)
static int g_adreno_state;        // 0 = not tried, 1 = custom active, -1 = not in use

// One-time: when a driver was selected, verify it makes sense here and open the hooked loader.
static void sw_adrenotools_try(void) {
    if (g_adreno_state) return;
    g_adreno_state = -1;
    const char *dir = getenv("SAVAGEWIND_VK_DRIVER_DIR"), *lib = getenv("SAVAGEWIND_VK_DRIVER_LIB");
    if (!dir || !*dir || !lib || !*lib) {
        sw_log("vk: using the system driver (no custom driver selected)");
        return;
    }

    char gpu[128] = "";
    bool adreno = sw_probe_is_adreno(gpu, sizeof gpu);
    sw_log(adreno ? "vk: system driver is Qualcomm Adreno - the selected custom driver applies"
                  : "vk: system driver is not Qualcomm Adreno - custom drivers are disabled here, "
                    "using the system driver");
    if (!adreno) return;

    // hookLibDir must be the APK's extracted library dir (MainActivity exports it); the hook
    // libraries (libhook_impl.so and friends) are packaged there. The custom driver itself lives
    // in the app's internal files dir (customDriverDir must not be on shared storage).
    const char *hookdir = getenv("SAVAGEWIND_LIB_DIR");
    if (!hookdir || !*hookdir) {
        sw_log("vk: SAVAGEWIND_LIB_DIR unset - using the system driver");
        return;
    }

    // Writable scratch for patched libraries (only used by adrenotools on API < 29).
    char tmp[1024];
    const char *tmpdir = getenv("TMPDIR");
    snprintf(tmp, sizeof tmp, "%s%s", tmpdir && *tmpdir ? tmpdir : "/data/local/tmp", "/adrenotools-tmp");
    mkdir(tmp, 0755);

    char msg[512];
    void *h = adrenotools_open_libvulkan(RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE, ADRENOTOOLS_DRIVER_CUSTOM,
                                         tmp, hookdir, dir, lib, NULL, NULL);
    if (!h) {
        sw_log("vk: adrenotools could not open libvulkan - falling back to the system driver");
        return;
    }
    g_adreno_libvulkan = h;
    g_adreno_state = 1;
    snprintf(msg, sizeof msg, "vk: custom Vulkan driver active: %s%s (system GPU: %s)", dir, lib, gpu);
    sw_log(msg);
}

// The instance-level entry point to use, honoring the AdrenoTools contract: with a custom
// driver, everything comes from the handle it returned.
static void *sw_vk_real(const char *name) {
    sw_adrenotools_try();
    if (g_adreno_libvulkan) {
        void *fn = dlsym(g_adreno_libvulkan, name);
        if (fn) return fn;
    }
    return dlsym(RTLD_NEXT, name);
}

bool plat_vk_custom_driver_active(void) { return g_adreno_state == 1; }

#elif defined(__ANDROID__)
// No AdrenoTools in this build: the system driver is always the one active.
bool plat_vk_custom_driver_active(void) { return false; }
static void *sw_vk_real(const char *name) { return dlsym(RTLD_NEXT, name); }

#else
static void *sw_vk_real(const char *name) { return dlsym(RTLD_NEXT, name); }
#endif

#if defined(__ANDROID__)
static VkPhysicalDeviceProperties g_probe_props;

// The GPU name as the active driver (custom when one was selected) reports it: a throwaway
// instance, one enumeration, done. Cached: the panel asks on every frame it draws.
void plat_vk_gpu_name(char *out, size_t n) {
    static int done;
    static char name[128];
    if (!done) {
        done = 1;
        name[0] = 0;
        VkInstance inst = NULL;
        VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_1};
        VkInstanceCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
        if (vkCreateInstance(&ci, NULL, &inst) == VK_SUCCESS && inst) {
            PFN_vkEnumeratePhysicalDevices enum_dev =
                (PFN_vkEnumeratePhysicalDevices)vkGetInstanceProcAddr(inst, "vkEnumeratePhysicalDevices");
            PFN_vkGetPhysicalDeviceProperties props =
                (PFN_vkGetPhysicalDeviceProperties)vkGetInstanceProcAddr(inst, "vkGetPhysicalDeviceProperties");
            PFN_vkDestroyInstance destroy = (PFN_vkDestroyInstance)vkGetInstanceProcAddr(inst, "vkDestroyInstance");
            uint32_t nd = 0;
            if (enum_dev && props && enum_dev(inst, &nd, NULL) == VK_SUCCESS && nd) {
                VkPhysicalDevice *pd = (VkPhysicalDevice *)calloc(nd, sizeof *pd);
                if (pd && enum_dev(inst, &nd, pd) == VK_SUCCESS) {
                    props(pd[0], &g_probe_props);
                    snprintf(name, sizeof name, "%s", g_probe_props.deviceName);
                }
                free(pd);
            }
            if (destroy) destroy(inst, NULL);
        }
    }
    snprintf(out, n, "%s", name[0] ? name : "?");
}
#endif

// Interposed instance-level entry points: the loader decides which driver serves the instance
// here, so these (and only these) must come from the AdrenoTools handle when one is active.
// Device-level calls (vkGetDeviceProcAddr, the vkCreateDevice shim below) dispatch through the
// instance the loader built, so they follow automatically.
VkResult vkCreateInstance(const VkInstanceCreateInfo *ci, const VkAllocationCallbacks *ac, VkInstance *out) {
    static PFN_vkCreateInstance real;
    if (!real) real = (PFN_vkCreateInstance)sw_vk_real("vkCreateInstance");
    return real ? real(ci, ac, out) : VK_ERROR_INITIALIZATION_FAILED;
}

VKAPI_ATTR PFN_vkGetInstanceProcAddr VKAPI_CALL vkGetInstanceProcAddr(VkInstance inst, const char *name) {
    static PFN_vkGetInstanceProcAddr real;
    if (!real) real = (PFN_vkGetInstanceProcAddr)sw_vk_real("vkGetInstanceProcAddr");
    return real ? real(inst, name) : NULL;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(const char *layer, uint32_t *count,
                                                                      VkExtensionProperties *props) {
    static PFN_vkEnumerateInstanceExtensionProperties real;
    if (!real) real = (PFN_vkEnumerateInstanceExtensionProperties)sw_vk_real("vkEnumerateInstanceExtensionProperties");
    return real ? real(layer, count, props) : VK_ERROR_INITIALIZATION_FAILED;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(uint32_t *count, VkLayerProperties *props) {
    static PFN_vkEnumerateInstanceLayerProperties real;
    if (!real) real = (PFN_vkEnumerateInstanceLayerProperties)sw_vk_real("vkEnumerateInstanceLayerProperties");
    return real ? real(count, props) : VK_ERROR_INITIALIZATION_FAILED;
}

static VkDevice g_vk_device;
static struct {
    PFN_vkCmdPipelineBarrier2 CmdPipelineBarrier2;
    PFN_vkGetDeviceBufferMemoryRequirements GetDeviceBufferMemoryRequirements;
    PFN_vkGetDeviceImageMemoryRequirements GetDeviceImageMemoryRequirements;
    PFN_vkGetSemaphoreCounterValue GetSemaphoreCounterValue;
    PFN_vkWaitSemaphores WaitSemaphores;
    PFN_vkCmdWriteTimestamp2 CmdWriteTimestamp2;
    PFN_vkCmdBeginRendering CmdBeginRendering;
    PFN_vkCmdEndRendering CmdEndRendering;
    PFN_vkQueueSubmit2 QueueSubmit2;
    PFN_vkCmdSetCullMode CmdSetCullMode;
    PFN_vkCmdSetDepthBiasEnable CmdSetDepthBiasEnable;
    PFN_vkCmdSetDepthCompareOp CmdSetDepthCompareOp;
    PFN_vkCmdSetDepthTestEnable CmdSetDepthTestEnable;
    PFN_vkCmdSetDepthWriteEnable CmdSetDepthWriteEnable;
    PFN_vkCmdSetFrontFace CmdSetFrontFace;
    PFN_vkCmdSetPrimitiveRestartEnable CmdSetPrimitiveRestartEnable;
    PFN_vkCmdSetPrimitiveTopology CmdSetPrimitiveTopology;
    PFN_vkCmdSetStencilOp CmdSetStencilOp;
    PFN_vkCmdSetStencilTestEnable CmdSetStencilTestEnable;
} p;

static void load_13(VkDevice d) {
    g_vk_device = d;
    p.CmdPipelineBarrier2 = (PFN_vkCmdPipelineBarrier2)vkGetDeviceProcAddr(d, "vkCmdPipelineBarrier2");
    p.GetDeviceBufferMemoryRequirements = (PFN_vkGetDeviceBufferMemoryRequirements)vkGetDeviceProcAddr(d, "vkGetDeviceBufferMemoryRequirements");
    p.GetDeviceImageMemoryRequirements = (PFN_vkGetDeviceImageMemoryRequirements)vkGetDeviceProcAddr(d, "vkGetDeviceImageMemoryRequirements");
    p.GetSemaphoreCounterValue = (PFN_vkGetSemaphoreCounterValue)vkGetDeviceProcAddr(d, "vkGetSemaphoreCounterValue");
    p.WaitSemaphores = (PFN_vkWaitSemaphores)vkGetDeviceProcAddr(d, "vkWaitSemaphores");
    p.CmdWriteTimestamp2 = (PFN_vkCmdWriteTimestamp2)vkGetDeviceProcAddr(d, "vkCmdWriteTimestamp2");
    p.CmdBeginRendering = (PFN_vkCmdBeginRendering)vkGetDeviceProcAddr(d, "vkCmdBeginRendering");
    p.CmdEndRendering = (PFN_vkCmdEndRendering)vkGetDeviceProcAddr(d, "vkCmdEndRendering");
    p.QueueSubmit2 = (PFN_vkQueueSubmit2)vkGetDeviceProcAddr(d, "vkQueueSubmit2");
    p.CmdSetCullMode = (PFN_vkCmdSetCullMode)vkGetDeviceProcAddr(d, "vkCmdSetCullMode");
    p.CmdSetDepthBiasEnable = (PFN_vkCmdSetDepthBiasEnable)vkGetDeviceProcAddr(d, "vkCmdSetDepthBiasEnable");
    p.CmdSetDepthCompareOp = (PFN_vkCmdSetDepthCompareOp)vkGetDeviceProcAddr(d, "vkCmdSetDepthCompareOp");
    p.CmdSetDepthTestEnable = (PFN_vkCmdSetDepthTestEnable)vkGetDeviceProcAddr(d, "vkCmdSetDepthTestEnable");
    p.CmdSetDepthWriteEnable = (PFN_vkCmdSetDepthWriteEnable)vkGetDeviceProcAddr(d, "vkCmdSetDepthWriteEnable");
    p.CmdSetFrontFace = (PFN_vkCmdSetFrontFace)vkGetDeviceProcAddr(d, "vkCmdSetFrontFace");
    p.CmdSetPrimitiveRestartEnable = (PFN_vkCmdSetPrimitiveRestartEnable)vkGetDeviceProcAddr(d, "vkCmdSetPrimitiveRestartEnable");
    p.CmdSetPrimitiveTopology = (PFN_vkCmdSetPrimitiveTopology)vkGetDeviceProcAddr(d, "vkCmdSetPrimitiveTopology");
    p.CmdSetStencilOp = (PFN_vkCmdSetStencilOp)vkGetDeviceProcAddr(d, "vkCmdSetStencilOp");
    p.CmdSetStencilTestEnable = (PFN_vkCmdSetStencilTestEnable)vkGetDeviceProcAddr(d, "vkCmdSetStencilTestEnable");
}

// The runtime creates its device with the (exported, 1.0) vkCreateDevice: intercept it once to
// learn the VkDevice, then hand everything through.
VkResult vkCreateDevice(VkPhysicalDevice pd, const VkDeviceCreateInfo *ci, const VkAllocationCallbacks *ac, VkDevice *out) {
    static PFN_vkCreateDevice real;
    if (!real) real = (PFN_vkCreateDevice)dlsym(RTLD_NEXT, "vkCreateDevice");
    VkResult r = real ? real(pd, ci, ac, out) : VK_ERROR_INITIALIZATION_FAILED;
    if (r == VK_SUCCESS) load_13(*out);
    return r;
}

VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier2(VkCommandBuffer cb, const VkDependencyInfo *info) {
    if (p.CmdPipelineBarrier2) p.CmdPipelineBarrier2(cb, info);
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceBufferMemoryRequirements(VkDevice d, const VkDeviceBufferMemoryRequirements *info,
                                         VkMemoryRequirements2 *reqs) {
    if (p.GetDeviceBufferMemoryRequirements) p.GetDeviceBufferMemoryRequirements(d, info, reqs);
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceImageMemoryRequirements(VkDevice d, const VkDeviceImageMemoryRequirements *info,
                                        VkMemoryRequirements2 *reqs) {
    if (p.GetDeviceImageMemoryRequirements) p.GetDeviceImageMemoryRequirements(d, info, reqs);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetSemaphoreCounterValue(VkDevice d, VkSemaphore sem, uint64_t *value) {
    return p.GetSemaphoreCounterValue ? p.GetSemaphoreCounterValue(d, sem, value) : VK_ERROR_DEVICE_LOST;
}

VKAPI_ATTR VkResult VKAPI_CALL vkWaitSemaphores(VkDevice d, const VkSemaphoreWaitInfo *info, uint64_t timeout) {
    return p.WaitSemaphores ? p.WaitSemaphores(d, info, timeout) : VK_ERROR_DEVICE_LOST;
}

VKAPI_ATTR void VKAPI_CALL vkCmdWriteTimestamp2(VkCommandBuffer cb, VkPipelineStageFlags2 stage, VkQueryPool pool,
                          uint32_t slot) {
    if (p.CmdWriteTimestamp2) p.CmdWriteTimestamp2(cb, stage, pool, slot);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginRendering(VkCommandBuffer cb, const VkRenderingInfo *info) {
    if (p.CmdBeginRendering) p.CmdBeginRendering(cb, info);
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndRendering(VkCommandBuffer cb) {
    if (p.CmdEndRendering) p.CmdEndRendering(cb);
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit2(VkQueue q, uint32_t count, const VkSubmitInfo2 *submits, VkFence fence) {
    return p.QueueSubmit2 ? p.QueueSubmit2(q, count, submits, fence) : VK_ERROR_DEVICE_LOST;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetCullMode(VkCommandBuffer cb, VkCullModeFlags mode) {
    if (p.CmdSetCullMode) p.CmdSetCullMode(cb, mode);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBiasEnable(VkCommandBuffer cb, VkBool32 enable) {
    if (p.CmdSetDepthBiasEnable) p.CmdSetDepthBiasEnable(cb, enable);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthCompareOp(VkCommandBuffer cb, VkCompareOp op) {
    if (p.CmdSetDepthCompareOp) p.CmdSetDepthCompareOp(cb, op);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthTestEnable(VkCommandBuffer cb, VkBool32 enable) {
    if (p.CmdSetDepthTestEnable) p.CmdSetDepthTestEnable(cb, enable);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthWriteEnable(VkCommandBuffer cb, VkBool32 enable) {
    if (p.CmdSetDepthWriteEnable) p.CmdSetDepthWriteEnable(cb, enable);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetFrontFace(VkCommandBuffer cb, VkFrontFace face) {
    if (p.CmdSetFrontFace) p.CmdSetFrontFace(cb, face);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetPrimitiveRestartEnable(VkCommandBuffer cb, VkBool32 enable) {
    if (p.CmdSetPrimitiveRestartEnable) p.CmdSetPrimitiveRestartEnable(cb, enable);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetPrimitiveTopology(VkCommandBuffer cb, VkPrimitiveTopology topology) {
    if (p.CmdSetPrimitiveTopology) p.CmdSetPrimitiveTopology(cb, topology);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilOp(VkCommandBuffer cb, VkStencilFaceFlags faces, VkStencilOp fail,
                       VkStencilOp pass, VkStencilOp depthFail, VkCompareOp compare) {
    if (p.CmdSetStencilOp) p.CmdSetStencilOp(cb, faces, fail, pass, depthFail, compare);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilTestEnable(VkCommandBuffer cb, VkBool32 enable) {
    if (p.CmdSetStencilTestEnable) p.CmdSetStencilTestEnable(cb, enable);
}
