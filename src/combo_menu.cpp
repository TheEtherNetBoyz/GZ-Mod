// GZ combo capture, labels, reset and inactivity timeout.
#include "core.hpp"
#include "combo_capture.hpp"
#include "menu_logic.hpp"
#include "gz_font.hpp"
#include "presentation.hpp"
#include <string_view>
namespace gz {
void resetCommandBinding(const char*);
namespace {
ComboCapture capture;
int row=0;
const char* ids[]={"combo_pause","combo_step","combo_timer","combo_timer_reset","combo_store","combo_load","combo_reload","combo_free_cam","combo_move_link","combo_gorge","combo_moon",
#ifdef GZ_LIVE_STATE
"combo_state_save","combo_state_load",
#endif
"combo_menu"};
const char* labels[]={"frame pause","frame advance","timer toggle","timer reset","store position","load position","reload area","free cam","move link","gorge void","moon jump",
#ifdef GZ_LIVE_STATE
"save state","load state",
#endif
"open/close menu"};
constexpr int rows=int(sizeof(ids)/sizeof(ids[0]));
Control* bindingControl(int i){for(auto& c:controls)if(c.id==ids[i])return &c;return nullptr;}
std::string comboText(uint16_t mask){
 const char* names[]={"DPad Left","DPad Right","DPad Down","DPad Up","Z","R","L","","A","B","X","Y","Start"};
 std::string result;
 for(int i=0;i<13;i++)if(mask&(1<<i)){if(!result.empty())result+="+";result+=names[i];}
 return result;
}
}
bool comboCaptureActive(){return capture.active;}
void cancelComboCapture(){capture.reset();}
bool comboMenuInput(std::string_view page,uint16_t buttons,uint16_t command,uint16_t edge){
 if(page!="Hotkeys"){capture.reset();return false;}
 if(capture.active){
  if(auto value=capture.update(buttons))if(auto* control=bindingControl(row))control->set(*value);
  return true;
 }
 if(edge&0x200)return false;
 row=int(singleColumnMove(row,rows,command));
 if(edge&0x400)resetCommandBinding(ids[row]);
 if(edge&0x100)capture.begin();
 return true;
}
bool drawComboMenu(std::string_view page){
 if(page!="Hotkeys")return false;
 const auto p=spritePosition(Menu);
 for(int i=0;i<rows;i++){
  auto* control=bindingControl(i);
  const auto mask=capture.active&&!capture.waitingSelect&&i==row?capture.previous:control?uint16_t(control->get()):0;
  const auto text=std::string("[")+comboText(mask)+"]";
  const auto color=i==row?cursorColor():capture.active?0x777777ff:0xffffffff;
  drawGzText(std::string(labels[i])+": ",p.x,p.y+i*20,color);
  drawGzText(text,p.x+gzTextWidth("open/close menu: "),p.y+i*20,color);
 }
 static const char* actions[]={"Pause the game","Advance the game by 1 frame","Toggle the timer","Reset the timer","Store the player's position","Load the player's position","Reload the area","Toggle Free Cam","Toggle Move Link","load the Gorge Void save file","Moon Jump",
#ifdef GZ_LIVE_STATE
"Save the game state","Load the game state",
#endif
"Open/close the GZ menu"};
 drawGzText(capture.active?"Press combination, then release a button to save":std::string("[Reset: X] Combo to ")+actions[row],p.x,440);
 return true;
}
}
