#ifndef GUARD_GBA_MEMORY_HOST_H
#define GUARD_GBA_MEMORY_HOST_H

#include <setjmp.h>
#include "gba/types.h"

// This header gets pulled in from inside gba/defines.h, which is included
// BEFORE gba/types.h in gba.h's own include order. Most files happened to
// already have u8/u16/u32 defined by the time they reached this point
// (something earlier in their own chain pulled types.h in first by
// coincidence) -- but not all of them, hence the scattered compile
// failures on specific files rather than all 283. Pulling types.h in here
// directly removes the dependency on include-order luck. Safe to include
// twice thanks to its own guard.

// Host-side stand-ins for GBA memory-mapped regions.
// On real hardware these are physical addresses (0x04000000, 0x06000000,
// etc.) wired to actual chips. On host, nothing lives at those addresses,
// so every one of pokefirered's REG_BASE-relative macros gets redirected
// here instead via -D flags / header override (see io_reg.h patch below).
//
// Sizes match real GBA memory-map region sizes exactly, so nothing the
// game does (bounds-wise) can be different from the real hardware:
//   I/O registers : 0x400   bytes  (1 KB   @ 0x04000000 on real hw)
//   Palette RAM   : 0x400   bytes  (1 KB   @ 0x05000000 on real hw)
//   VRAM          : 0x18000 bytes  (96 KB  @ 0x06000000 on real hw)
//   OAM           : 0x400   bytes  (1 KB   @ 0x07000000 on real hw)

#define GBA_IOREG_SIZE  0x400
#define GBA_PLTT_SIZE   0x400
#define GBA_VRAM_SIZE   0x18000
#define GBA_OAM_SIZE    0x400

extern u8 gGbaIoRegs[GBA_IOREG_SIZE];
extern u8 gGbaPltt[GBA_PLTT_SIZE];
extern u8 gGbaVram[GBA_VRAM_SIZE];
extern u8 gGbaOam[GBA_OAM_SIZE];

// Real hardware: these three live in the last 16 bytes of IWRAM
// (0x3007FF0 / 0x3007FF8 / 0x3007FFC), a spot the BIOS reserves.
// main.c's InitIntrHandlers()/VBlankIntr()/etc. write through these
// addresses DIRECTLY -- they are not covered by the REG_BASE or
// PLTT/VRAM/OAM redirects, so they need their own host backing or
// InitIntrHandlers() segfaults on its first call.
extern struct SoundInfo *gGbaSoundInfoPtr;
extern u16 gGbaIntrCheck;
extern void *gGbaIntrVector;

// Real hardware: SoftReset() jumps back to the ROM entry point. Host
// equivalent: main() does setjmp() right before its first call to
// AgbMain(); SoftReset() re-inits emulated memory then longjmps back here,
// which re-enters AgbMain() from the top -- same effective behavior.
extern jmp_buf gAgbMainResetPoint;

void InitGbaMemoryHost(void);

#endif // GUARD_GBA_MEMORY_HOST_H
