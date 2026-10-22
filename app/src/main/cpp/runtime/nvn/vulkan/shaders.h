// Shader compilation and reflection for the Vulkan backend (shaders.c).
#pragma once
#include <stddef.h>

#include "vk.h"

typedef struct { uint32_t set, binding; VkDescriptorType type; } VkBindingInfo;

void vk_shaders_init(void);
uint32_t *vk_glsl_to_spirv(const char *src, int stage, const char *name, size_t *nwords);  // malloc'd, NULL on error
// maxwell_translate through a cache (memory and cache/xlate/): the same ShaderInfo for the same binary and key, never
// freed. Failed translations are not cached.
ShaderInfo *vk_translate_cached(const uint8_t *code, size_t max_size, int stage, const ShaderKey *key);
// The id of a translation vk_translate_cached returned (its cache/xlate name; 0: not cached), and the translation
// with that id from memory or cache/xlate/ (NULL: not there, or not of that stage).
uint64_t vk_translate_id(const ShaderInfo *si);
ShaderInfo *vk_translate_load_id(uint64_t id, int stage);
// cache/ (or SAVAGEWIND_CACHE_DIR): out = that folder + "/" + name
const char *vk_cache_dir(void);
void vk_cache_path(char *out, size_t n, const char *name);
// The translator's build and the SPIR-V post-processing version: what caches of translated shaders are valid for.
const char *vk_shader_version(void);
VkShaderModule vk_module(const uint32_t *spv, size_t nwords);
// Descriptor bindings declared by a SPIR-V module.
int vk_reflect(const uint32_t *spv, size_t nwords, VkBindingInfo *out, int max);
VkDescriptorSetLayout vk_set_layout(const VkBindingInfo *b, int n, uint32_t set, VkShaderStageFlags stages);
