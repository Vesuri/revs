/* Host-only SDK/register stand-ins for test_amiga_input.py. No analogue physics model. */
#include <stdint.h>
#define _UTIL_H
#define _AMIGAHARDWARE_H
struct Library {};
struct Node { int ln_Type, ln_Pri; char* ln_Name; };
struct Interrupt { Node is_Node; void* is_Data; void (*is_Code)(); };
typedef const char* CONST_STRPTR;
#define CIAANAME "ciaa"
#define CIAICRB_SP 3
#define CIACRAF_SPMODE 64
#define NT_INTERRUPT 2
static Library lib;
static void* OpenResource(const char*) { return &lib; }
static Interrupt* AddICRVector(Library*, int, Interrupt*) { return 0; }
static void RemICRVector(Library*, int, Interrupt*) {}
static volatile uint16_t testJoy0, testJoy1, testPot1, testPotInp=0xffff, testPotGo;
static volatile uint8_t testPra=0xff, testSdr, testCra;
#define joy0datPointer (&testJoy0)
#define joy1datPointer (&testJoy1)
#define pot1datPointer (&testPot1)
#define potinpPointer (&testPotInp)
#define potgoPointer (&testPotGo)
#define ciaapraPointer (&testPra)
#define ciaasdrPointer (&testSdr)
#define ciaacraPointer (&testCra)
