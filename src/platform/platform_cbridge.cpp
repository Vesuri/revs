/* C bridge — implements the C-callable interface declared in platform_c.h by
   forwarding to the Platform C++ singleton.  Compiled as C++ so it can see
   platform.h; the symbols are exported with C linkage so the generated C
   translation units link against them unmangled. */

#include "platform.h"
#include "platform_c.h"

extern "C" {

uint8_t platform_hw_read(uint16_t addr) {
    return platform ? platform->hwRead(addr) : 0;
}

void platform_hw_write(uint16_t addr, uint8_t val) {
    if (platform) platform->hwWrite(addr, val);
}

void platform_shadow_write(uint16_t addr, uint8_t val) {
    if (platform) platform->shadowWrite(addr, val);
}

int platform_load_image(const char* path) {
    return platform ? platform->loadImage(path) : -1;
}

void platform_register_vbi(uint16_t addr, void (*fn)(void)) {
    if (platform) platform->registerVBI(addr, fn);
}

void platform_indirect_jmp(uint16_t addr) {
    if (platform) platform->indirectJmp(addr);
}

void platform_mos_call(uint16_t entry) {
    if (platform) platform->mosCall(entry);
}

void platform_render_frame(void) {
    if (platform) platform->renderFrame();
}

void platform_tick_vbi(void) {
    if (platform) platform->tickVBI();
}

void platform_poll_events(void) {
    if (platform) platform->pollEvents();
}

} /* extern "C" */

/* ---------------------------------------------------------------------------
   Test-only headless platform for the native-twin validation harness
   (tools/validate_native.c).  The harness links the platform objects but never
   opens a window.  None of this is referenced by the real game build.

   ⚠ Keep this minimal and DETERMINISTIC.  Anything the harness needs the
   platform to emulate (a MOS call a twin makes, a VIA timer a twin polls) must
   be modelled here identically for BOTH the twin and its __t6502 oracle, or the
   differential is comparing two different machines.  See
   docs/validation-harness.md.
   --------------------------------------------------------------------------- */
extern volatile uint8_t mem[65536];   /* the 6502 RAM image (src/cpu/cpu.c) */

/* Opt-in for frame-wait fixtures: when nonzero, tickVBI advances the BBC's
   100 Hz system clock low byte so a frame-driven twin and its oracle both make
   progress instead of spinning forever on a never-changing counter.  Default OFF
   so every pure-mem[] test is unaffected. */
static int g_headlessTickClock = 0;
static uint16_t g_headlessClockAddr = 0x0292;   /* MOS TIME low byte — confirm */

namespace {
struct HeadlessPlatform : Platform {
    void    run() override {}
    void    setInterrupt(void (*)(void)) override {}
    int     framesPerSecond() override { return 50; }
    void    renderFrame() override {}
    void    tickVBI() override { if (g_headlessTickClock) mem[g_headlessClockAddr]++; }
    int     loadImage(const char*) override { return -1; }
    uint8_t hwRead(uint16_t) override { return 0x00; }
};
} /* namespace */

extern "C" {

void platform_test_init_headless(void) {
    if (!platform) platform = new HeadlessPlatform();
}

/* Enable/disable the clock-advancing tick for frame-wait twin validation. */
void platform_test_tick_clock(int on) { g_headlessTickClock = on ? 1 : 0; }
void platform_test_clock_addr(uint16_t a) { g_headlessClockAddr = a; }

} /* extern "C" */
