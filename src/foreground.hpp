#pragma once
#include "core.hpp"
#include <array>
#include <cstdint>
#include <span>
namespace gz {
ModResult initForeground();
void shutdownForeground();
bool beginForeground();
uint64_t foregroundResourceTexture(const char*);
uint64_t foregroundTexture(unsigned,unsigned,const void*);
uint64_t foregroundConsoleTexture(unsigned,unsigned,const void*);
void foregroundQuad(uint64_t,float,float,float,float,float,float,float,float,uint32_t);
void foregroundPixelAlignedRect(float centerX,float centerY,float width,float height,uint32_t color);
void foregroundCircle(float centerX,float centerY,float radius,uint32_t color);
void foregroundLine(float,float,float,float,uint32_t,float);
// A convex loop with distinct vertices, in perimeter order.
void foregroundClosedOutline(std::span<const std::array<float,2>>,uint32_t,float);
void foregroundTriangle(float,float,float,float,float,float,uint32_t);
}
