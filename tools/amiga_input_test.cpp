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

 // Paddle toggle: retain X calibration, ignore every Y value and mouse buttons.
 // Release/repress without a VBI must still count as a new press.
 key(RK_F2,false);key(RK_F2,true);assert(revs_key_down(0x8d));
 settled(0x0050);assert(input.axis(1)==0xff00 && input.axis(2)==0x8000);
 assert(revs_key_down(0x8d));assert(input.axis(2)==0x8000); // held: no repeated toggle
 input.pressBbcKey(0xae,true);assert(input.axis(2)==0xff00);
 settled(0x5000);assert(input.axis(1)==0 && input.axis(2)==0xff00);
 input.pressBbcKey(0xbe,true);assert(input.axis(2)==0x8000); // both pedals: neutral
 input.pressBbcKey(0xae,false);assert(input.axis(2)==0);
 input.pressBbcKey(0xbe,false);testPotInp=0xfbff;testPra=0xbf;sample();
 assert(input.axis(2)==0x8000);testPotInp=0xffff;testPra=0xff;
 testJoy1=0x0200;sample();assert(input.buttons()==1);testJoy1=0;sample();
 input.pressBbcKey(0x9f,true);assert(revs_key_down(0x9f));input.pressBbcKey(0x9f,false);
 input.pressBbcKey(0xef,true);assert(revs_key_down(0xef));input.pressBbcKey(0xef,false);
 key(RK_F2,false);key(RK_F2,true);assert(revs_key_down(0x8d));
 settled(0x0050);assert(input.axis(1)==0xff00 && input.axis(2)==0xff00); // joystick again
 input.pressBbcKey(0xbe,true);assert(input.axis(2)==0xff00);input.pressBbcKey(0xbe,false);
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
 // Keyboard -> F2 always enters joystick, even when the last source was paddle.
 settled(0x1212);key(RK_F2,true);assert(revs_key_down(0x8d));key(RK_F2,false);
 settled(0x0024);assert(input.axis(2)==0xff00);
 key(RK_F2,true);assert(revs_key_down(0x8d));key(RK_F2,false);assert(input.axis(2)==0x8000);
 key(RK_F1,true);assert(revs_key_down(0x8e));key(RK_F1,false);
 settled(0x1212);key(RK_F2,true);assert(revs_key_down(0x8d));key(RK_F2,false);
 settled(0x0024);assert(input.axis(2)==0xff00);
 // Latched F2 taps toggle exactly once, including two taps between VBI samples.
 g_screenMode7=1;key(RK_F2,true);key(RK_F2,false);assert(revs_key_down(0x8d));
 assert(input.axis(2)==0x8000);assert(!revs_key_down(0x8d));
 key(RK_F2,true);key(RK_F2,false);assert(revs_key_down(0x8d));assert(input.axis(2)==0xff00);
 g_screenMode7=0;
 key(RK_F2,true);assert(revs_key_down(0x8d));key(RK_F2,false);assert(input.axis(2)==0x8000);
 // A front-end tap follows the same selector path as a held race key.
 g_screenMode7=1;key(RK_F3,true);key(RK_F3,false);assert(revs_key_down(0x8d));
 assert(input.axis(1)==0x8100);assert(!revs_key_down(0x8d));g_screenMode7=0;
 // Mouse -> F2 starts joystick even after leaving paddle for mouse.
 settled(0x1212);key(RK_F2,true);assert(revs_key_down(0x8d));
 settled(0x0024);assert(input.axis(2)==0xff00);key(RK_F2,false);
 // Paddle compensates the engine curve; check the mathematical result rather
 // than a copy of the lookup table. Nearest squared value has <=127 error.
 for(unsigned v=0;v<256;v++) {
  const unsigned result=paddleSteering((uint16_t)(v<<8))>>8;
  const unsigned m=v>=128?v-128:127-v;
  const unsigned q=result>=128?result-128:127-result;
  const int error=(int)(q*q)-(int)(127*m);
  assert(error>=-127 && error<=127);
  assert(q!=10); // Nurburgring's isolated magnitude-10 branch cannot fire.
  assert((result>=128)==(v>=128));
  if(v) assert(result>=(paddleSteering((uint16_t)((v-1)<<8))>>8));
  if(m==0 || m==127) assert(result==v);
  // Expansion tracks: their floor(k*q/256), square, double curve remains
  // linear within the inverse-square error and both floor errors.
  // |q*q-127*m|<=127; |floor(z)^2-z^2|<2*z.
  const unsigned scales[]={0xb5,0xf0,0xf8,0xbe,0xdc,0xc3,0xd7,0xcd,0xbc,0xd4,0xca};
  for(unsigned k:scales) {
   const unsigned a=k*q/256, aMax=k*127/256;
   const double ideal=2.0*aMax*aMax*m/127.0;
   const double delta=2.0*a*a-ideal;
   const double t=k/256.0;
   const double bound=2.0*(127*t*t + 2*t*q + 2*t*m);
   assert(delta>=-bound && delta<=bound);
  }
 }
 // Real mode selection applies compensation only to paddle steering.
 settled(0x2828);key(RK_F1,true);assert(revs_key_down(0x8e));key(RK_F1,false);
 key(RK_F2,true);assert(revs_key_down(0x8d));key(RK_F2,false);
 settled(0x283c);const uint16_t joystickMid=input.axis(1);
 key(RK_F2,true);assert(revs_key_down(0x8d));key(RK_F2,false);
 assert(input.axis(1)==paddleSteering(joystickMid) && input.axis(1)>joystickMid);
 key(RK_F2,true);assert(revs_key_down(0x8d));key(RK_F2,false);
 assert(input.axis(1)==joystickMid);
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
