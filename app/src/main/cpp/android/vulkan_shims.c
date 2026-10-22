// Vulkan 1.2/1.3 core entry points the runtime calls directly. Android's Vulkan loader only
// exports 1.0/1.1 symbols: anything newer must come from vkGetDeviceProcAddr. Rather than change
// the vendored backend (runtime/nvn/vulkan), these shims carry the same C signatures and forward
// to the real driver functions, learned when the runtime creates its device (the only one).
#include <dlfcn.h>

#include <vulkan/vulkan.h>

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
