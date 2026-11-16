#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace gz {
using OutlinePoint = std::array<float, 2>;
using OutlineJoin = std::array<OutlinePoint, 2>;

inline float outlinePixelRadius(float canvasWidth, float verticalScale) {
    return std::max(1.f, std::round(canvasWidth * verticalScale)) * .5f;
}

inline OutlineJoin outlineJoin(OutlinePoint previous, OutlinePoint point, OutlinePoint next,
                               OutlinePoint pixelScale, float radius) {
    const auto normal = [pixelScale](OutlinePoint a, OutlinePoint b) -> OutlinePoint {
        const float dx = (b[0] - a[0]) * pixelScale[0];
        const float dy = (b[1] - a[1]) * pixelScale[1];
        const float inverseLength = 1.f / std::sqrt(dx * dx + dy * dy);
        return {-dy * inverseLength, dx * inverseLength};
    };
    const auto incoming = normal(previous, point);
    const auto outgoing = normal(point, next);
    // The miter intersects the two parallel offset edges at the corner.
    const float divisor = 1.f + incoming[0] * outgoing[0] + incoming[1] * outgoing[1];
    const float mx = (incoming[0] + outgoing[0]) * radius / (divisor * pixelScale[0]);
    const float my = (incoming[1] + outgoing[1]) * radius / (divisor * pixelScale[1]);
    return {{{point[0] - mx, point[1] - my}, {point[0] + mx, point[1] + my}}};
}
}
