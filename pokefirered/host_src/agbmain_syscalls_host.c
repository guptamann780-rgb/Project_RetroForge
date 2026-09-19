#include "gba/types.h"
#include "gba/defines.h"
#include "gba/io_reg.h"
#include "main.h"
#include "gba_memory_host.h"
#include <setjmp.h>

extern void Host_RunFrame(void);

// Real BIOS call: block until the next VBlank interrupt. On host this
// converges onto the exact same single-threaded frame-pump WaitForVBlank()
// in main.c now calls (see main_c_patch_instructions.txt) -- there is no
// second source of "vblank" on host, so every caller, wherever it lives,
// drives the same real tick: input, VBlankIntr() dispatch, render, pace.
//
// KNOWN LIMITATION: if something calls this DURING a frame that
// WaitForVBlank() already ticked (rather than as main.c's own call site),
// you get an extra tick that frame rather than a true "wait for the SAME
// vblank" semantic. Flagging this the same way you flagged
// ObjAffineSet's struct fields -- verify against real call sites once
// something beyond main.c actually calls VBlankIntrWait directly.
void VBlankIntrWait(void)
{
    Host_RunFrame();
}

// Real BIOS call: jump back to the ROM entry point, i.e. restart
// execution from crt0. Host equivalent: re-init emulated GBA memory
// (palette/VRAM/OAM/registers/heap all go back to zero, matching what
// RegisterRamReset would have done) then longjmp back to the setjmp()
// main_sdl.c's main() planted right before its first AgbMain() call.
//
// VERIFY: exact resetFlags semantics (u8 bitmask vs bool) unconfirmed --
// same caveat class as your ObjAffineSet field-name flag. DoSoftReset()
// in main.c calls this as SoftReset(RESET_ALL & ~RESET_SIO_REGS); we
// currently ignore resetFlags entirely and always do a full memory
// re-init, which is safe (does at least as much as any real flag
// combination would) but not flag-accurate.
void SoftReset(u32 resetFlags)
{
    (void)resetFlags;
    InitGbaMemoryHost();

// (b) your host RegisterRamReset, when (flags & 0x80) or RESET_ALL
REG_BG2PA = 0x100; REG_BG2PD = 0x100; REG_BG3PA = 0x100; REG_BG3PD = 0x100;
    longjmp(gAgbMainResetPoint, 1);
}
