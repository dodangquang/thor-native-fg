#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
// Exact std430 layout shared with shaders/quality.glsl.
struct QualityStats {
 uint32_t rgbSum{},lumaSum{},changed{},valid{},count{},boundary{},ambiguous{},reliable{};
 std::array<uint32_t,64> previousHistogram{},currentHistogram{};
 uint32_t holdoff{},allow{},reasons{},historySerial{};
};
static_assert(sizeof(QualityStats)==560);
static_assert(offsetof(QualityStats,holdoff)==544);
enum QualityReason : uint32_t {
 QUALITY_CUT=1u, QUALITY_HOLDOFF=2u, QUALITY_LOW_FLOW=4u,
 QUALITY_FAST_MOTION=8u, QUALITY_WARMUP=16u
};
