#include "gba/types.h"
#include "main.h"

// Real crt0.s/rom_header.s content -- deliberately discarded per your
// earlier audit (asm/ = macros only, real boot code lives in src/*.s).
// These three symbols only need to EXIST with sane sizes; nothing on
// host actually dispatches through them the way real hardware would.

// intr_main is a real ARM machine-code blob on hardware, DMA-copied into
// IntrMain_Buffer by InitIntrHandlers() in main.c:
//   DmaCopy32(3, intr_main, IntrMain_Buffer, sizeof(IntrMain_Buffer));
// On host that copy is harmless busywork (host_src/main_sdl.c dispatches
// interrupts directly via gIntrTable[], never through IntrMain_Buffer/
// INTR_VECTOR), but the source buffer must be at least as large as
// IntrMain_Buffer (u32[0x200] in main.c) or DmaCopy32 over-reads past it.
u32 intr_main[0x200] = {0};

const char RomHeaderGameCode[GAME_CODE_LENGTH] = "BPRE";
const char RomHeaderSoftwareVersion = 0;
