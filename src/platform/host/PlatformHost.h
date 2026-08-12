#pragma once
/* PlatformHost — the macOS/Linux development backend.
 *
 * ⚠ READ THIS BEFORE ADDING A RENDERER HERE.
 *
 * The Atari port's equivalent was an SDL backend that drew the emulated screen RAM to a
 * window, and the single most expensive lesson of that project was: **the dev-host
 * backend is not ground truth.**  It is an approximation of the real machine, and hours
 * went into bugs that were only ever bugs in the approximation.  The standing rule there
 * became "validate against the 6502 + the reference emulator, NOT the host backend".
 *
 * So this port deliberately starts the host build WITHOUT a renderer.  The host build
 * exists for the two things it is genuinely better at than the target:
 *
 *   1. `make validate` — the byte-exact differential between a native twin and its
 *      transliterated oracle (tools/validate_native.c).  Fast, deterministic, millions
 *      of randomised cases in seconds.
 *   2. Host-side algebra proofs — compile the old and new bodies of a restructured
 *      routine side by side over randomised inputs before writing any m68k asm.
 *      (docs/method-lessons.md — this is what made a "structural" ceiling fall.)
 *
 * Visual ground truth comes from jsbeeb / b2 running the real disc
 * (docs/bbc-reference-loop.md), and performance ground truth from FS-UAE + gdb on the
 * real Amiga build (docs/headless-fsuae.md).  Neither job needs a window here.
 *
 * If a host renderer later earns its place, add it — but as a debugging aid that is
 * never cited as evidence, and expect to state that in every comment that mentions it.
 */
#include "../platform.h"

#define PlatformClass PlatformHost

class PlatformHost : public Platform {
public:
    explicit PlatformHost(const char* imagePath);
    virtual ~PlatformHost();

    virtual void    run() override;
    virtual void    setInterrupt(void (*fn)(void)) override;
    virtual int     framesPerSecond() override;
    virtual void    renderFrame() override;
    virtual void    tickVBI() override;
    virtual int     loadImage(const char* path) override;

private:
    void (*vbi)(void);
    unsigned long frames;
};
