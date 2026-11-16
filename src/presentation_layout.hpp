#pragma once
#include <algorithm>
namespace gz {
struct OverlayViewport{float left=0,top=0,width=0,height=0,canvasWidth=608;};
inline constexpr unsigned MaxCircleSegments=128;
inline unsigned circleSegments(float pixelRadius){
 // Keep the gap between a chord and the circle below a quarter pixel.
 if(pixelRadius<=13.f)return 16;
 if(pixelRadius<=51.f)return 32;
 if(pixelRadius<=207.f)return 64;
 return MaxCircleSegments;
}
inline OverlayViewport overlayViewport(float width,float height){
 if(width<=0||height<=0)return {};
 if(width/height>=4.f/3)return {0,0,width,height,608.f*(width/height)/(4.f/3)};
 const float fit=width/640.f;
 return {0,(height-480.f*fit)*.5f,width,480.f*fit,608};
}
inline float positionToWide(float saved,float canvas,float extent){
 const float baseSpace=std::max(1.f,608.f-extent);
 return std::clamp(saved/baseSpace,0.f,1.f)*std::max(0.f,canvas-extent);
}
inline float positionFromWide(float x,float canvas,float extent){
 const float space=std::max(1.f,canvas-extent);
 return std::clamp(x/space,0.f,1.f)*std::max(1.f,608.f-extent);
}
}
