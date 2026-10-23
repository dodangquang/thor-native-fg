// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <optional>

namespace thor {
struct CompositeLayout {
    uint32_t width, height, left, top, right, bottom;
};

// Preserve display geometry while compositing at the source's pixel density.
inline std::optional<CompositeLayout> sourceComposite(CompositeLayout display,
                                                      uint32_t sourceWidth,
                                                      uint32_t sourceHeight) {
    if (display.width < 16 || display.height < 16 || display.width > 4096 ||
        display.height > 4096 || display.left >= display.right ||
        display.top >= display.bottom || display.right > display.width ||
        display.bottom > display.height || sourceWidth < 16 || sourceHeight < 16 ||
        sourceWidth > 4096 || sourceHeight > 4096) return {};
    uint64_t numerator = sourceWidth, denominator = display.right - display.left;
    if (uint64_t(sourceHeight) * denominator < numerator * (display.bottom - display.top)) {
        numerator = sourceHeight;
        denominator = display.bottom - display.top;
    }
    if (numerator >= denominator) return {};
    const uint32_t width = uint64_t(display.width) * numerator / denominator;
    const uint32_t height = uint64_t(display.height) * numerator / denominator;
    if (width < 16 || height < 16 || width >= display.width || height >= display.height) return {};
    CompositeLayout result{width, height,
        uint32_t(uint64_t(display.left) * width / display.width),
        uint32_t(uint64_t(display.top) * height / display.height),
        uint32_t(uint64_t(display.right) * width / display.width),
        uint32_t(uint64_t(display.bottom) * height / display.height)};
    if (result.right - result.left < 16 || result.bottom - result.top < 16) return {};
    return result;
}
}
