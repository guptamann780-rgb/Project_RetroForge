#include "gba/types.h"
#include "gba/m4a_internal.h"

// umul3232H32 is NOT audio-specific -- it's the high 32 bits of a plain
// 32x32->64 unsigned multiply (real hardware did this with a UMULL
// instruction, see the .s: `umull r2, r3, r0, r1` then returns r3, the
// high word). Portable C gives us this for free via a 64-bit intermediate,
// no deferral needed, no ambiguity about correctness.
u32 umul3232H32(u32 multiplier, u32 multiplicand)
{
    return (u32)(((u64)multiplier * (u64)multiplicand) >> 32);
}

// Everything below is REAL sound engine logic, deliberately stubbed per
// your "sound is a fancy problem, tackle last" plan. Two different
// reasons a stub is safe here, not one:
//
//  (a) SoundMain / SoundMainRAM / SoundMainBTM / MPlayMain / TrackStop /
//      RealClearChain / m4aSoundVSync -- these are called for real from
//      already-compiled src/*.c (m4a.c, main.c's VCountIntr). Making them
//      no-ops means "no audio processing happens each frame", which is
//      exactly the intended behavior right now: silent, not broken.
//
//  (b) MPlayJumpTableCopy / all 20 ply_* handlers -- these are stored as
//      DATA (function pointers) in m4a_tables.o's dispatch tables, not
//      called directly by any C code path that's currently live. Since
//      MPlayMain (the only thing that would ever dispatch through that
//      table) is itself a no-op above, these never actually run yet --
//      they just need to be valid, linkable addresses.

void SoundMain(void) {}
// SoundMainRAM is declared `extern char SoundMainRAM[];` in
// m4a_internal.h, NOT as a function -- real hardware tail-jumps into it
// directly from SoundMain's asm (`bx r3`), so pret declares it as a bare
// address rather than a C function pointer. Since SoundMain is a no-op
// here, nothing ever jumps into this; it only needs to exist as a valid
// address of the declared type.
char SoundMainRAM[1] = {0};
void SoundMainBTM(void) {}
void MPlayMain(struct MusicPlayerInfo *mplayInfo) { (void)mplayInfo; }
void TrackStop(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    (void)track;
}
void RealClearChain(void *x) { (void)x; }
void m4aSoundVSync(void) {}
void MPlayJumpTableCopy(MPlayFunc *mplayJumpTable) { (void)mplayJumpTable; }

void ply_fine(struct MusicPlayerInfo *a, struct MusicPlayerTrack *b) { (void)a; (void)b; }
void ply_goto(struct MusicPlayerInfo *a, struct MusicPlayerTrack *b) { (void)a; (void)b; }
void ply_patt(struct MusicPlayerInfo *a, struct MusicPlayerTrack *b) { (void)a; (void)b; }
void ply_pend(struct MusicPlayerInfo *a, struct MusicPlayerTrack *b) { (void)a; (void)b; }
void ply_rept(struct MusicPlayerInfo *a, struct MusicPlayerTrack *b) { (void)a; (void)b; }
void ply_prio(struct MusicPlayerInfo *a, struct MusicPlayerTrack *b) { (void)a; (void)b; }
void ply_tempo(struct MusicPlayerInfo *a, struct MusicPlayerTrack *b) { (void)a; (void)b; }
void ply_keysh(struct MusicPlayerInfo *a, struct MusicPlayerTrack *b) { (void)a; (void)b; }
void ply_voice(struct MusicPlayerInfo *a, struct MusicPlayerTrack *b) { (void)a; (void)b; }
void ply_vol(struct MusicPlayerInfo *a, struct MusicPlayerTrack *b) { (void)a; (void)b; }
void ply_pan(struct MusicPlayerInfo *a, struct MusicPlayerTrack *b) { (void)a; (void)b; }
void ply_bend(struct MusicPlayerInfo *a, struct MusicPlayerTrack *b) { (void)a; (void)b; }
void ply_bendr(struct MusicPlayerInfo *a, struct MusicPlayerTrack *b) { (void)a; (void)b; }
void ply_lfos(struct MusicPlayerInfo *a, struct MusicPlayerTrack *b) { (void)a; (void)b; }
void ply_lfodl(struct MusicPlayerInfo *a, struct MusicPlayerTrack *b) { (void)a; (void)b; }
void ply_mod(struct MusicPlayerInfo *a, struct MusicPlayerTrack *b) { (void)a; (void)b; }
void ply_modt(struct MusicPlayerInfo *a, struct MusicPlayerTrack *b) { (void)a; (void)b; }
void ply_tune(struct MusicPlayerInfo *a, struct MusicPlayerTrack *b) { (void)a; (void)b; }
void ply_port(struct MusicPlayerInfo *a, struct MusicPlayerTrack *b) { (void)a; (void)b; }
void ply_endtie(struct MusicPlayerInfo *a, struct MusicPlayerTrack *b) { (void)a; (void)b; }
void ply_note(u32 note_cmd, struct MusicPlayerInfo *a, struct MusicPlayerTrack *b)
{
    (void)note_cmd;
    (void)a;
    (void)b;
}
