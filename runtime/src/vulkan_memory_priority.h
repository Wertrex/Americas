// VK_EXT_memory_priority: when VRAM is oversubscribed the OS demotes the lowest-priority allocations to system memory
// first. The title's streamed textures are the bulk of the allocations and tolerate slow memory; the buffers every draw
// reads (the arena, the title windows, the shadow buffers) do not, so they get the highest priority and the texture
// blocks the lowest. OFF by default: with the title's textures already past the card's memory it made more texture
// allocations fail and the title crashed within two minutes in three runs out of three (run56/58/59), and ran 4 minutes
// and more without it (run53-55, run60). LEONIDA_MEMORY_PRIORITY=1 turns it on.
#pragma once

#include <vulkan/vulkan.h>

#include <atomic>
#include <cstdlib>

namespace leonida {

inline std::atomic<bool>& memory_priority_enabled() {
    static std::atomic<bool> on{false};
    return on;
}

inline bool memory_priority_wanted() {
    const char* v = std::getenv("LEONIDA_MEMORY_PRIORITY");
    return v && v[0] == '1';
}

inline constexpr float kPriorityBuffers = 1.0f;
inline constexpr float kPriorityRenderImages = 0.8f;
inline constexpr float kPriorityTextureBlocks = 0.1f;

}  // namespace leonida
