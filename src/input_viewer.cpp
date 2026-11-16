// Geometry and layout adapted directly from TPGZ 1.2.0-dev input_viewer.cpp (GPL-3.0).
#include "gz_font.hpp"
#include "helpers/gx_helper.h"
#include "m_Do/m_Do_controller_pad.h"
#include <cmath>
#include <cstdio>
#include <numbers>
namespace gz { namespace {

struct Vec2{float x,y;};
enum class GZPad:uint16_t{DPAD_LEFT=1,DPAD_RIGHT=2,DPAD_DOWN=4,DPAD_UP=8,Z=16,R=32,L=64,A=256,B=512,X=1024,Y=2048,START=4096};
static JUTGamePad::CStick mPadMStick,mPadSStick;
static JUTGamePad::CButton mPadButton;
static struct {int stick_x=0,stick_y=0,substick_x=0,substick_y=0;} mPadStatus;
bool GZ_getButtonPressed(GZPad b){return (mPadButton.mButton&static_cast<uint16_t>(b))!=0;}
namespace Font {
float getCharWidth(char c,float size){return gzTextWidth(std::string(1,c),size);}
void GZ_drawChar(char c,float x,float y,uint32_t color,bool,float size){drawGzTextPlain(std::string(1,c),x,y,color,size);}
void GZ_drawStr(const char* s,float x,float y,uint32_t color,bool,float size){drawGzTextPlain(s,x,y,color,size);}
}
namespace Draw {
void begin(unsigned n,unsigned primitive){beginGzShape(n,primitive);}
void begin_outline(unsigned n,unsigned width){beginGzShape(n,GX_LINESTRIP,width);}
void add_vertex(uint32_t c,Vec2 p,Vec2){gzShapeVertex(p.x,p.y,c);}
void end(){endGzShape();}
void drawRect(uint32_t c,Vec2 p,Vec2 d){
 begin(4,GX_TRIANGLESTRIP);add_vertex(c,p,{});add_vertex(c,{p.x+d.x,p.y},{});
 add_vertex(c,{p.x,p.y+d.y},{});add_vertex(c,{p.x+d.x,p.y+d.y},{});end();
}
void drawRectOutline(uint32_t c,Vec2 p,Vec2 d,unsigned w){
 begin_outline(5,w);add_vertex(c,p,{});add_vertex(c,{p.x+d.x,p.y},{});
 add_vertex(c,{p.x+d.x,p.y+d.y},{});add_vertex(c,{p.x,p.y+d.y},{});add_vertex(c,p,{});end();
}
}
#define OUTLINE_WIDTH 0x10

void drawEllipse(uint32_t color, Vec2 center, Vec2 dim) {
    constexpr const size_t n_points = 4;
    float dx[n_points] = {0};
    float dy[n_points] = {0};
    float tmp;
    for (size_t i = 0; i < n_points; ++i) {
        dx[i] = cos(i * M_PI / 2.0 / n_points);
        dy[i] = sin(i * M_PI / 2.0 / n_points);
    }
    Draw::begin(4 * n_points, GX_TRIANGLEFAN);
    for (uint8_t i = 0; i < 4; ++i) {
        for (size_t j = 0; j < n_points; ++j) {
            Draw::add_vertex(color, {center.x + dim.x / 2 * dx[j], center.y + dim.y / 2 * dy[j]},
                             {0.5f + dx[j], 0.5f + dy[j]});
            tmp = dx[j];
            dx[j] = -dy[j];
            dy[j] = tmp;
        }
    }
    Draw::end();
}

void drawEllipseOutline(uint32_t color, Vec2 center, Vec2 dim, uint8_t line_width) {
    constexpr const size_t n_points = 4;
    float dx[n_points] = {0};
    float dy[n_points] = {0};
    float tmp;
    for (size_t i = 0; i < n_points; ++i) {
        dx[i] = cos(i * M_PI / 2.0 / n_points);
        dy[i] = sin(i * M_PI / 2.0 / n_points);
    }
    Draw::begin_outline(4 * n_points + 1, line_width);
    for (uint8_t i = 0; i < 4; ++i) {
        for (size_t j = 0; j < n_points; ++j) {
            Draw::add_vertex(color, {center.x + dim.x / 2 * dx[j], center.y + dim.y / 2 * dy[j]},
                             {0.5f + dx[j], 0.5f + dy[j]});
            tmp = dx[j];
            dx[j] = -dy[j];
            dy[j] = tmp;
        }
    }
    Draw::add_vertex(color, {center.x + dim.x / 2 * dx[0], center.y + dim.y / 2 * dy[0]},
                     {0.5f + dx[0], 0.5f + dy[0]});
    Draw::end();
}

void drawStickOutline(uint32_t color, Vec2 pos, float size, float x_ratio) {
    constexpr float diagonal = 0.7071f;
    constexpr std::array<std::array<float, 2>, 8> gate = {{
        {0, -1}, {diagonal, -diagonal}, {1, 0}, {diagonal, diagonal},
        {0, 1}, {-diagonal, diagonal}, {-1, 0}, {-diagonal, -diagonal},
    }};
    auto points = gate;
    const float radius = size / 2;
    for (auto& [x, y] : points) {
        x = pos.x + radius * x * x_ratio;
        y = pos.y + radius * y;
    }
    drawGzClosedOutline(points, color, OUTLINE_WIDTH);
}

uint32_t drawButtonBody(GZPad button, uint32_t color, Vec2 pos, Vec2 dim) {
    bool is_button_down = GZ_getButtonPressed(button);
    Draw::drawRectOutline(color, {pos.x, pos.y}, dim, OUTLINE_WIDTH);
    if (is_button_down) {
        Draw::drawRect(color, {pos.x, pos.y}, dim);
    }
    return is_button_down ? 0x00000060 : color;
}

void drawButton(GZPad button, char c, uint32_t color, Vec2 pos, Vec2 dim, float char_size) {
    const uint32_t labelColor = drawButtonBody(button, color, pos, dim);
    float width = Font::getCharWidth(c, char_size);
    Font::GZ_drawChar(c, pos.x + (dim.x - width) / 2, pos.y + (dim.y + char_size / 2) / 2,
                      labelColor, false, char_size);
}

void drawButtonEllipse(GZPad button, char c, uint32_t color, Vec2 pos, Vec2 dim, float char_size) {
    bool is_button_down = GZ_getButtonPressed(button);
    drawEllipseOutline(color, {pos.x + dim.x / 2, pos.y + dim.y / 2}, dim, OUTLINE_WIDTH);
    if (is_button_down) {
        drawEllipse(color, {pos.x + dim.x / 2, pos.y + dim.y / 2}, dim);
    }
    float width = Font::getCharWidth(c, char_size);
    Font::GZ_drawChar(c, pos.x + (dim.x - width) / 2, pos.y + (dim.y + char_size / 2) / 2,
                      is_button_down ? 0x00000060 : color, false, char_size);
}

void drawCross(uint32_t color, Vec2 pos, float size, float x_ratio) {
    float branch_width = 3.f * size / 11.f;
    float branch_length = 4.f * size / 11.f;
    const float markerLength = 3.5f * size / 25.f;
    const float markerWidth = .5f * size / 25.f;
    const Vec2 horizontal = {branch_length * x_ratio, branch_width};
    const Vec2 vertical = {branch_width * x_ratio, branch_length};
    const auto drawDirection = [&](GZPad button, Vec2 position, Vec2 dimensions, Vec2 marker) {
        const uint32_t markerColor = drawButtonBody(button, color, position, dimensions);
        drawGzPixelAlignedRect(position.x + dimensions.x / 2, position.y + dimensions.y / 2,
                               marker.x, marker.y, markerColor);
    };
    drawDirection(GZPad::DPAD_LEFT, {pos.x, pos.y + branch_length},
                  horizontal, {markerLength, markerWidth});
    drawDirection(GZPad::DPAD_UP, {pos.x + branch_length * x_ratio, pos.y},
                  vertical, {markerWidth, markerLength});
    drawDirection(GZPad::DPAD_RIGHT,
                  {pos.x + (branch_length + branch_width) * x_ratio, pos.y + branch_length},
                  horizontal, {markerLength, markerWidth});
    drawDirection(GZPad::DPAD_DOWN,
                  {pos.x + branch_length * x_ratio, pos.y + branch_length + branch_width},
                  vertical, {markerWidth, markerLength});
}

static void drawViewer(Vec2 pos, float scale, bool is_shadow, bool wide_screen) {
    float x_ratio = wide_screen ? 0.75f : 1.0f;

    drawButton(GZPad::A, 'A', is_shadow ? 0x00000060 : 0x00FF7fFF,
               {pos.x + 130.f * scale, pos.y + 30.f * scale}, {30.f * scale, 30.f * scale},
               8 * scale);
    drawButton(GZPad::B, 'B', is_shadow ? 0x00000060 : 0xFF0000FF,
               {pos.x + 108.5f * scale, pos.y + 45.f * scale}, {13.f * scale, 13.f * scale},
               8 * scale);
    drawButton(GZPad::X, 'X', is_shadow ? 0x00000060 : 0xFFFFFFFF,
               {pos.x + 167.5f * scale, pos.y + 30.f * scale}, {15.f * scale, 30.f * scale},
               8 * scale);
    drawButton(GZPad::Y, 'Y', is_shadow ? 0x00000060 : 0xFFFFFFFF,
               {pos.x + 130.f * scale, pos.y + 7.5f * scale}, {30.f * scale, 15.f * scale},
               8 * scale);
    drawButton(GZPad::Z, 'Z', is_shadow ? 0x00000060 : 0x8A2BE2FF,
               {pos.x + 167.5f * scale, pos.y + 7.5f * scale}, {15.f * scale, 15.f * scale},
               8 * scale);
    drawButton(GZPad::START, ' ', is_shadow ? 0x00000060 : 0xFFFFFFFF,
               {pos.x + 87.5f * scale, pos.y + 40.f * scale}, {10.f * scale, 10.f * scale},
               8 * scale);
    drawCross(is_shadow ? 0x00000060 : 0xFFFFFFFF, {pos.x + 95.f * scale, pos.y + 10.f * scale},
              25.f * scale, x_ratio);

    drawStickOutline(is_shadow ? 0x00000060 : 0xFFFFFFFF,
                     {pos.x + 17.5f * scale, pos.y + 30.f * scale}, 35.0f * scale, x_ratio);
    drawStickOutline(is_shadow ? 0x00000060 : 0xFFD138FF,
                     {pos.x + 62.5f * scale, pos.y + 30.f * scale}, 35.0f * scale, x_ratio);
    drawGzCircle(pos.x + (17.5f + mPadMStick.mPosX * 10) * scale,
                 pos.y + (30.f - mPadMStick.mPosY * 10) * scale,
                 10.f * scale, is_shadow ? 0x00000060 : 0xFFFFFFFF);
    drawGzCircle(pos.x + (62.5f + mPadSStick.mPosX * 10) * scale,
                 pos.y + (30.f - mPadSStick.mPosY * 10) * scale,
                 10.f * scale, is_shadow ? 0x00000060 : 0xFFD138FF);

    Draw::drawRectOutline(is_shadow ? 0x00000060 : 0xFFFFFFFF, {pos.x, pos.y},
                          {35.f * scale, 7.f * scale}, OUTLINE_WIDTH);
    Draw::drawRect(is_shadow ? 0x00000060 : GZ_getButtonPressed(GZPad::L) ? 0x00FF00FF : 0xFFFFFFFF,
                   {pos.x, pos.y}, {35.f * mPadButton.mAnalogLf * scale, 7.f * scale});
    Draw::drawRectOutline(is_shadow ? 0x00000060 : 0xFFFFFFFF, {pos.x + 45.f * scale, pos.y},
                          {35.f * scale, 7.f * scale}, OUTLINE_WIDTH);
    Draw::drawRect(is_shadow ? 0x00000060 : GZ_getButtonPressed(GZPad::R) ? 0x00FF00FF : 0xFFFFFFFF,
                   {pos.x + (45.f + 35.f * (1 - mPadButton.mAnalogRf)) * scale, pos.y},
                   {35.f * mPadButton.mAnalogRf * scale, 7.f * scale});

    char control_x[5];
    char control_y[5];
    char c_x[5];
    char c_y[5];

    sprintf(control_x, "%3d", mPadStatus.stick_x);
    sprintf(control_y, "%3d", mPadStatus.stick_y);
    sprintf(c_x, "%3d", mPadStatus.substick_x);
    sprintf(c_y, "%3d", mPadStatus.substick_y);

    Font::GZ_drawStr(control_x, pos.x, pos.y + 65.f * scale, is_shadow ? 0x00000060 : 0xFFFFFFFF,
                     false, 13 * scale);
    Font::GZ_drawStr(control_y, pos.x + 23.f * scale, pos.y + 65.f * scale,
                     is_shadow ? 0x00000060 : 0xFFFFFFFF, false, 13 * scale);
    Font::GZ_drawStr(c_x, pos.x + 45.f * scale, pos.y + 65.f * scale,
                     is_shadow ? 0x00000060 : 0xFFD138FF, false, 13 * scale);
    Font::GZ_drawStr(c_y, pos.x + 70.f * scale, pos.y + 65.f * scale,
                     is_shadow ? 0x00000060 : 0xFFD138FF, false, 13 * scale);


}
}
void sampleInputViewer(){
 if(auto* pad=mDoCPd_c::getGamePad(0)){
  mPadMStick=pad->mMainStick;mPadSStick=pad->mSubStick;mPadButton=pad->mButton;
  mPadStatus={mPadMStick.mRawX,mPadMStick.mRawY,mPadSStick.mRawX,mPadSStick.mRawY};
 }else{mPadButton.mButton=0;mPadButton.mAnalogLf=0;mPadButton.mAnalogRf=0;
  mPadMStick.mPosX=mPadMStick.mPosY=mPadSStick.mPosX=mPadSStick.mPosY=0;mPadStatus={};}
}
void drawInputViewer(float x,float y){
 if(on("drop_shadows"))drawViewer({x+1,y+1},1,true,false);drawViewer({x,y},1,false,false);
}
}
