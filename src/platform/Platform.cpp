#include "platform.h"

extern volatile uint8_t mem[65536];

Platform* platform = nullptr;

/* ⚠ The singleton is claimed HERE, in the base constructor, not in each backend's — the
   C bridge (platform_cbridge.cpp) and the scene both reach the platform through it, so a
   backend that forgot to set it produced a null-deref crash AFTER a clean takeover: the
   VBI counter kept ticking while nothing rendered, which reads like a render bug rather
   than a null pointer.  (Cost one headless round trip during scaffolding.) */
Platform::Platform() : quit(false) { platform = this; }
Platform::~Platform() { if (platform == this) platform = 0; }

uint8_t Platform::hwRead(uint16_t)           { return 0x00; }
void    Platform::hwWrite(uint16_t, uint8_t) {}
void    Platform::shadowWrite(uint16_t, uint8_t) {}
