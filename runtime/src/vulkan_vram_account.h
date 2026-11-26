// LOCAL DIAGNOSTIC (LEONIDA_VRAM_ACCOUNT=1): counts every vkAllocateMemory / vkFreeMemory of the device by kind and
// memory type, so a log line can say what holds the device memory. The device-function tables install it when they load.
#pragma once

#include <vulkan/vulkan.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>

namespace leonida::vram {

enum Kind { plain, dedicated_image, dedicated_buffer, host_import, kind_count };
inline const char* kind_name(int k) {
    static const char* names[] = {"plain", "dedicated image", "dedicated buffer", "host import"};
    return names[k];
}

struct State {
    std::mutex mutex;
    PFN_vkAllocateMemory real_alloc = nullptr;
    PFN_vkFreeMemory real_free = nullptr;
    struct Entry { std::uint32_t type; std::uint64_t size; int kind; };
    std::unordered_map<VkDeviceMemory, Entry> live;
    std::map<std::pair<std::uint32_t, int>, std::pair<std::uint64_t, std::uint64_t>> by_type_kind;  // bytes, count
    std::uint64_t total_allocated = 0, total_count = 0, peak = 0, current = 0;
};
inline State& state() { static State s; return s; }

inline bool enabled() {
    static const bool on = [] { const char* v = std::getenv("LEONIDA_VRAM_ACCOUNT"); return v && v[0] == '1'; }();
    return on;
}

inline VKAPI_ATTR VkResult VKAPI_CALL account_alloc(VkDevice device, const VkMemoryAllocateInfo* info,
                                                    const VkAllocationCallbacks* callbacks, VkDeviceMemory* memory) {
    State& s = state();
    const VkResult r = s.real_alloc(device, info, callbacks, memory);
    if (r != VK_SUCCESS || !info || !memory) return r;
    int kind = plain;
    for (auto* p = static_cast<const VkBaseInStructure*>(info->pNext); p; p = p->pNext) {
        if (p->sType == VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT) kind = host_import;
        else if (p->sType == VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO && kind == plain) {
            const auto* d = reinterpret_cast<const VkMemoryDedicatedAllocateInfo*>(p);
            kind = d->image ? dedicated_image : d->buffer ? dedicated_buffer : plain;
        }
    }
    std::lock_guard lock(s.mutex);
    s.live[*memory] = {info->memoryTypeIndex, info->allocationSize, kind};
    auto& slot = s.by_type_kind[{info->memoryTypeIndex, kind}];
    slot.first += info->allocationSize; ++slot.second;
    s.total_allocated += info->allocationSize; ++s.total_count;
    s.current += info->allocationSize; if (s.current > s.peak) s.peak = s.current;
    return r;
}

inline VKAPI_ATTR void VKAPI_CALL account_free(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks* callbacks) {
    State& s = state();
    {
        std::lock_guard lock(s.mutex);
        if (const auto it = s.live.find(memory); it != s.live.end()) {
            auto& slot = s.by_type_kind[{it->second.type, it->second.kind}];
            slot.first -= it->second.size; --slot.second;
            s.current -= it->second.size;
            s.live.erase(it);
        }
    }
    s.real_free(device, memory, callbacks);
}

// Called by VulkanDeviceFunctions::load with its freshly loaded pointers.
inline void install(PFN_vkAllocateMemory& alloc, PFN_vkFreeMemory& release) {
    if (!enabled() || !alloc || !release) return;
    State& s = state();
    if (alloc == &account_alloc) return;
    if (!s.real_alloc) { s.real_alloc = alloc; s.real_free = release; }
    alloc = &account_alloc;
    release = &account_free;
}

inline void report() {
    if (!enabled()) return;
    State& s = state();
    std::lock_guard lock(s.mutex);
    std::string line = "[rt] vram: live " + std::to_string(s.current >> 20) + " MiB, peak " + std::to_string(s.peak >> 20) + " MiB;";
    for (const auto& [key, v] : s.by_type_kind) {
        if (!v.second) continue;
        line += " type" + std::to_string(key.first) + "/" + kind_name(key.second) + " " + std::to_string(v.first >> 20) +
                " MiB x" + std::to_string(v.second) + ";";
    }
    std::fprintf(stderr, "%s\n", line.c_str());
}

}  // namespace leonida::vram
