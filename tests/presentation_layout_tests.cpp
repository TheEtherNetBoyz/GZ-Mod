#include "outline_geometry.hpp"
#include "presentation_layout.hpp"
#include <cmath>
#include <array>
#include <numbers>

template<size_t N>
bool checkOutline(const std::array<gz::OutlinePoint,N>& points,gz::OutlinePoint scale){
 using namespace gz;
 const float radius=outlinePixelRadius(16.f/6.f,scale[1]);
 std::array<OutlineJoin,N> joins;
 for(size_t i=0;i<N;++i)
  joins[i]=outlineJoin(points[(i+N-1)%N],points[i],points[(i+1)%N],scale,radius);

 for(size_t i=0;i<N;++i){
  const size_t j=(i+1)%N;
  const float dx=(points[j][0]-points[i][0])*scale[0];
  const float dy=(points[j][1]-points[i][1])*scale[1];
  const float length=std::hypot(dx,dy);
  for(size_t end:{i,j}){
   const float x=(joins[end][1][0]-joins[end][0][0])*scale[0];
   const float y=(joins[end][1][1]-joins[end][0][1])*scale[1];
   if(std::abs((dx*y-dy*x)/length-2*radius)>.001f)return false;
  }
 }
 return true;
}

int main(){
 using namespace gz;
 auto a=overlayViewport(1920,1080),b=overlayViewport(2560,1080),c=overlayViewport(640,480);
 if(std::abs(a.width/a.canvasWidth-b.width/b.canvasWidth)>.001f)return 1;
 if(a.left||b.left||c.canvasWidth!=608)return 2;
 for(float extent:{32.f,158.f,320.f,500.f})for(float fraction:{0.f,.25f,.5f,1.f}){
  float x=fraction*(b.canvasWidth-extent);
  float saved=positionFromWide(x,b.canvasWidth,extent);
  if(std::abs(positionToWide(saved,b.canvasWidth,extent)-x)>.001f)return 3;
  float narrow=positionToWide(saved,608,extent);
  if(narrow<0||narrow+extent>608.001f)return 4;
  if(std::abs(positionToWide(saved,b.canvasWidth,extent)-x)>.001f)return 5;
 }
 if(positionToWide(10000,608,158)!=450)return 6;
 auto portrait=overlayViewport(720,1280);
 if(portrait.canvasWidth!=608||portrait.top<=0)return 7;
 if(overlayViewport(0,1080).width)return 8;
 constexpr float diagonal=17.5f*.7071f;
 constexpr std::array<OutlinePoint,8> gate={{{0,-17.5f},{diagonal,-diagonal},{17.5f,0},
   {diagonal,diagonal},{0,17.5f},{-diagonal,diagonal},{-17.5f,0},{-diagonal,-diagonal}}};
 if(outlinePixelRadius(16.f/6.f,1.f)!=1.5f||outlinePixelRadius(16.f/6.f,4.f)!=5.5f)return 9;
 for(auto viewport:{overlayViewport(608,448),c,overlayViewport(2432,1792),b,
                    overlayViewport(721,1280)}){
  const OutlinePoint scale={viewport.width/viewport.canvasWidth,viewport.height/448.f};
  if(!checkOutline(gate,scale))return 10;
 }
 if(circleSegments(10.f)!=16||circleSegments(40.f)!=32)return 11;
 for(float radius:{13.f,14.f,51.f,52.f,207.f,208.f,400.f}){
  const auto segments=circleSegments(radius);
  if(MaxCircleSegments%segments||radius*(1-std::cos(std::numbers::pi/segments))>.25)return 12;
 }
}
