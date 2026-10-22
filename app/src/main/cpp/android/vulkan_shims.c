// Vulkan 1.2/1.3 core entry points the runtime calls directly. Android's Vulkan loader only
// exports 1.0/1.1 symbols: anything newer must come from vkGetDeviceProcAddr. Rather than change
// the vendored backend (runtime/nvn/vulkan), these shims carry the same C signatures and forward
// to the real driver functions, learned when the runtime creates its device (the only one).
#include <dlfcn.h>

#include <vulkan/vulkan.h>

static VkDevice g_vk_device;
static PFN_vkCmdPipelineBarrier2 g_barrier2;
static PFN_vkGetDeviceBufferMemoryRequirements g_buf_reqs;
static PFN_vkGetDeviceImageMemoryRequirements g_img_reqs;
static void load_13(VkDevice d) {
    g_vk_device = d;
    g_barrier2 = (PFN_vkCmdPipelineBarrier2)vkGetDeviceProcAddr(d, "vkCmdPipelineBarrier2");
    g_buf_reqs = (PFN_vkGetDeviceBufferMemoryRequirements)vkGetDeviceProcAddr(d, "vkGetDeviceBufferMemoryRequirements");
    g_img_reqs = (PFN_vkGetDeviceImageMemoryRequirements)vkGetDeviceProcAddr(d, "vkGetDeviceImageMemoryRequirements");
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

void vkCmdPipelineBarrier2(VkCommandBuffer cb, const VkDependencyInfo *info) {
    if (g_barrier2) g_barrier2(cb, info);
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceBufferMemoryRequirements(VkDevice d, const VkDeviceBufferMemoryRequirements *info,
                                         VkMemoryRequirements2 *reqs) {
    if (g_buf_reqs) g_buf_reqs(d, info, reqs);
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceImageMemoryRequirements(VkDevice d, const VkDeviceImageMemoryRequirements *info,
                                        VkMemoryRequirements2 *reqs) {
    if (g_img_reqs) g_img_reqs(d, info, reqs);
}
