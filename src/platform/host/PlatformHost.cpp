/* PlatformHost — headless development backend.  See PlatformHost.h for why it has no
   renderer. */
#include "PlatformHost.h"

#include <cstdio>
#include <cstring>

extern "C" volatile uint8_t mem[65536];

PlatformHost::PlatformHost(const char* imagePath) : vbi(0), frames(0)
{
    if (loadImage(imagePath) != 0) {
        std::fprintf(stderr, "PlatformHost: cannot load memory image '%s'\n",
                     imagePath ? imagePath : "(null)");
        std::fprintf(stderr, "  build it first:  python3 tools/ssd_load.py revs.ssd disasm\n");
        quit = true;
    }
}

PlatformHost::~PlatformHost() {}   /* the base dtor releases the singleton */

int PlatformHost::loadImage(const char* path)
{
    if (!path) return -1;
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return -1;
    unsigned char buf[65536];
    std::size_t n = std::fread(buf, 1, sizeof(buf), f);
    std::fclose(f);
    for (std::size_t i = 0; i < n; i++) mem[i] = buf[i];
    return 0;
}

void PlatformHost::setInterrupt(void (*fn)(void)) { vbi = fn; }
int  PlatformHost::framesPerSecond()              { return 50; }

void PlatformHost::renderFrame()
{
    /* No display.  Advance the frame clock so frame-driven code makes progress. */
    frames++;
    if (vbi) vbi();
}

void PlatformHost::tickVBI() { renderFrame(); }

void PlatformHost::run()
{
    /* TODO(phase: C transliteration): call the genuine entry chain here, exactly as the
       Amiga backend does, so the host and the target run the SAME code and only the
       platform differs.  Until then, report the image loaded so the scaffold is
       verifiable end to end. */
    std::printf("PlatformHost: image loaded, no entry chain wired yet.\n");
    std::printf("  mem[$1200..$1207] = %02X %02X %02X %02X %02X %02X %02X %02X\n",
                mem[0x1200], mem[0x1201], mem[0x1202], mem[0x1203],
                mem[0x1204], mem[0x1205], mem[0x1206], mem[0x1207]);
}
