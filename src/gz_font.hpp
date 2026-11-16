#pragma once
#include "core.hpp"
#include <array>
#include <span>
namespace gz {
ModResult initGzFont();
void shutdownGzFont();
void beginGzDraw();
void endGzDraw();
float gzTextWidth(const std::string&,float size=17);
void drawGzTextPlain(const std::string&,float,float,uint32_t,float);
void beginGzShape(unsigned count,unsigned primitive,unsigned lineWidth=16);
void gzShapeVertex(float,float,uint32_t);
void endGzShape();
void drawGzPixelAlignedRect(float centerX,float centerY,float width,float height,uint32_t color);
void drawGzCircle(float centerX,float centerY,float radius,uint32_t color);
void drawGzClosedOutline(std::span<const std::array<float,2>>,uint32_t,unsigned lineWidth);
void drawGzText(const std::string&,float x,float y,uint32_t color=0xffffffff,float size=17);
}
