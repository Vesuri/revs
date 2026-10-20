/* Exercise the production input code, with only OS calls and hardware registers mocked. */
#include <cassert>
#include <cstdio>
#include "../src/platform/amiga/RevsInput.cpp"
#include "../src/platform/amiga/revs_keys.h"
extern "C" { volatile uint16_t g_vbiCount=0, g_screenMode7=0; volatile uint8_t g_keyDirect=1; }
static RevsInput input;
extern "C" int platform_key_down(uint8_t c) { return input.keyDown(c); }
static void key(uint8_t raw, bool down) {
 uint8_t code=raw | (down ? 0 : 0x80);
 testSdr=~(uint8_t)((code<<1)|(code>>7)); keyboardHandler();
}
static void sample(uint16_t pots=0x2828) { testPot1=pots; ++g_vbiCount; input.sampleMouse(); }
static void settled(uint16_t p) { for(int i=0;i<4;i++) sample(p); }
int main() {
 assert(input.initialize()); assert(!testPotGo); assert(!g_keyMapOverflow);
 assert(input.axis(1)==0x8000); sample(); assert(testPotGo==1); sample();
 key(RK_LSHIFT,true);key(RK_F2,true);
 assert(revs_key_down(0x8d)); assert(input.axis(1)==0x8000 && input.axis(2)==0x8000);
 sample(0x2850);assert(input.axis(1)>0x8000 && input.axis(1)<0xff00);assert(input.axis(2)==0x8000);
 // Holding selector must not re-centre as the stick moves.
 settled(0x0050);assert(revs_key_down(0x8d));assert(input.axis(1)==0xff00 && input.axis(2)==0xff00);
 settled(0x5000);assert(input.axis(1)==0 && input.axis(2)==0);
 testJoy1=0x0200;sample();assert(input.buttons()==1);testJoy1=0;sample();assert(!input.buttons());

 key(RK_F2,false);sample();key(RK_F3,true);assert(revs_key_down(0x8d));assert(!revs_key_down(0x8c));
 assert(input.axis(1)==0x8000 && input.axis(2)==0x8000);
 testPotInp=0xfbff;sample();assert(input.axis(2)==0xff00 && !input.buttons());
 testPotInp=0xfeff;sample();assert(input.buttons()==1 && input.axis(2)==0x8000);
 testPotInp=0xffff;testPra=0xbf;sample();assert(input.axis(2)==0);testPra=0xff;
 sample();testPotInp=0;assert(input.axis(2)==0x8000 && !input.buttons()); // discharge cannot leak
 testPotInp=0xffff;testJoy0=254;sample();assert(input.axis(1)==0x8100); // existing mouse sign and half sensitivity
 key(RK_F3,false);key(RK_F8,true);assert(revs_key_down(0x8c));assert(!revs_key_down(0x8a));key(RK_F8,false);
 key(RK_F9,true);assert(revs_key_down(0x8a));key(RK_F9,false);key(RK_F6,true);assert(!revs_key_down(0x8a));key(RK_F6,false);
 // Re-select captures a new centre; right shift also works.
 key(RK_LSHIFT,false);key(RK_RSHIFT,true);settled(0x1212);key(RK_F2,true);assert(revs_key_down(0x8d));
 assert(input.axis(1)==0x8000 && input.axis(2)==0x8000);settled(0x0024);assert(input.axis(1)==0xff00 && input.axis(2)==0xff00);
 // The ordinary keyboard selector and both new assistance bindings remain separate.
 key(RK_F2,false);key(RK_F1,true);assert(revs_key_down(0x8e));assert(!revs_key_down(0x8d));key(RK_F1,false);
 // A front-end tap follows the same selector path as a held race key.
 g_screenMode7=1;key(RK_F3,true);key(RK_F3,false);assert(revs_key_down(0x8d));
 assert(input.axis(1)==0x8100);assert(!revs_key_down(0x8d));g_screenMode7=0;
 // Check monotonicity, centre, direction, and output bounds for every counter value.
 unsigned checks=0;
 for(unsigned c=2;c<254;c++) for(unsigned v=0;v<256;v++) {
  auto a=joystickAxis(v,c,false), b=joystickAxis(v,c,true);
  assert(!(a&255) && !(b&255));
  if(v) { assert(a>=joystickAxis(v-1,c,false)); assert(b<=joystickAxis(v-1,c,true)); }
  if(v+1>=c && v<=c+1) assert(a==0x8000 && b==0x8000);
  if(v<c-1) assert(a<0x8000 && b>0x8000);
  ++checks;
 }
 assert(joystickAxis(255,0,false)==0x8000);
 assert(joystickAxis(0,255,false)==0x8000);
 input.shutdown(); printf("PASS: real input implementation, keyboard IRQ, fast key path, mouse/joystick isolation; %u axis cases\n",checks);
}
