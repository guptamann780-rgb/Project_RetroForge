// libagbsyscall_host.c
//
// Portable C reimplementations of the GBA BIOS (SWI) calls this game
// actually uses. Ported from src/libagbsyscall.s — that file stays
// untouched for the real ARM build; this file is host-only.
//
// Formulas cross-checked against GBATEK's BIOS Function documentation
// (https://mgba-emu.github.io/gbatek/#biosfunctions). Where a struct
// layout could not yet be confirmed against this project's real
// headers, it is marked // VERIFY below.

#include "gba/types.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include "gba/multiboot.h"
#include "gba/syscall.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

//-----------------------------------------------------------------------
// ArcTan2 (SWI 0x0A)
//-----------------------------------------------------------------------
u16 ArcTan2(s16 x, s16 y)
{
    double rad = atan2((double)y, (double)x);

    if (rad < 0.0)
        rad += 2.0 * M_PI;

    u32 angle = (u32)(rad * (65536.0 / (2.0 * M_PI)));
    return (u16)(angle & 0xFFFF);
}

//-----------------------------------------------------------------------
// BgAffineSet (SWI 0x0E)
//
// Real struct (as given):
//   struct BgAffineSrcData { s32 texX, texY; s16 scrX, scrY; s16 sx, sy; u16 alpha; };
//   struct BgAffineDstData { s16 pa, pb, pc, pd; s32 dx, dy; };
//
// GBATEK formula: pa = sx*cos(a), pb = -sx*sin(a), pc = sy*sin(a), pd = sy*cos(a)
//                 dx = texX - (pa*scrX + pb*scrY)
//                 dy = texY - (pc*scrX + pd*scrY)
// Only the UPPER 8 bits of `alpha` are used (real hardware quirk).
//-----------------------------------------------------------------------
void BgAffineSet(struct BgAffineSrcData *src, struct BgAffineDstData *dest, s32 count)
{
    for (s32 i = 0; i < count; i++)
    {
        u8 angleIdx = (u8)(src[i].alpha >> 8);
        double rad = (angleIdx / 256.0) * (2.0 * M_PI);
        double cosVal = cos(rad);
        double sinVal = sin(rad);

        s16 pa = (s16)round(src[i].sx * cosVal);
        s16 pb = (s16)round(-src[i].sx * sinVal);
        s16 pc = (s16)round(src[i].sy * sinVal);
        s16 pd = (s16)round(src[i].sy * cosVal);

        dest[i].pa = pa;
        dest[i].pb = pb;
        dest[i].pc = pc;
        dest[i].pd = pd;

        dest[i].dx = src[i].texX - ((s32)pa * src[i].scrX + (s32)pb * src[i].scrY);
        dest[i].dy = src[i].texY - ((s32)pc * src[i].scrX + (s32)pd * src[i].scrY);
    }
}

//-----------------------------------------------------------------------
// CpuFastSet (SWI 0x0C)
//
// NOTE: real hardware rounds the word count up to a multiple of 8 and
// may touch a few words past the requested length. On host this is a
// real out-of-bounds-write risk if src/dest aren't padded to match.
// Left in place to match hardware behavior exactly, but flagged here
// so it isn't forgotten once real buffers are wired up.
//-----------------------------------------------------------------------
void CpuFastSet(const void *src, void *dest, u32 control)
{
    u32 words = control & 0x1FFFFF;
    if (words == 0)
        return;

    u32 roundedWords = (words + 7) & ~(u32)7;

    if (control & CPU_FAST_SET_SRC_FIXED)
    {
        u32 fillValue = *(const u32 *)src;
        u32 *d = (u32 *)dest;
        for (u32 i = 0; i < roundedWords; i++)
            d[i] = fillValue;
    }
    else
    {
        const u32 *s = (const u32 *)src;
        u32 *d = (u32 *)dest;
        for (u32 i = 0; i < roundedWords; i++)
            d[i] = s[i];
    }
}

//-----------------------------------------------------------------------
// CpuSet (SWI 0x0B)
//-----------------------------------------------------------------------
void CpuSet(const void *src, void *dest, u32 control)
{
    u32 count = control & 0x1FFFFF;
    if (count == 0)
        return;

    int is32Bit = (control & CPU_SET_32BIT) != 0;
    int isFixed = (control & CPU_SET_SRC_FIXED) != 0;

    if (is32Bit)
    {
        u32 *d = (u32 *)dest;
        const u32 *s = (const u32 *)src;
        if (isFixed)
        {
            u32 fillVal = *s;
            for (u32 i = 0; i < count; i++) d[i] = fillVal;
        }
        else
        {
            for (u32 i = 0; i < count; i++) d[i] = s[i];
        }
    }
    else
    {
        u16 *d = (u16 *)dest;
        const u16 *s = (const u16 *)src;
        if (isFixed)
        {
            u16 fillVal = *s;
            for (u32 i = 0; i < count; i++) d[i] = fillVal;
        }
        else
        {
            for (u32 i = 0; i < count; i++) d[i] = s[i];
        }
    }
}

//-----------------------------------------------------------------------
// Div (SWI 0x06)
//
// Changed from the original while(1); on divide-by-zero — that hangs
// the whole game silently with no way to tell what happened. This
// version fails loudly on stderr and returns 0 instead, so a bad
// caller shows up immediately in your terminal rather than a frozen
// window you have to guess about.
//-----------------------------------------------------------------------
s32 Div(s32 num, s32 denom)
{
    if (denom == 0)
    {
        fprintf(stderr, "Div: division by zero (num=%d)\n", num);
        return 0;
    }
    return num / denom;
}

//-----------------------------------------------------------------------
// LZ77UnCompVram (SWI 0x12) — 16-bit halfword-buffered decompression,
// matching VRAM's real write-width restriction.
//-----------------------------------------------------------------------
void LZ77UnCompVram(const void *src, void *dest)
{
    const u8 *in = (const u8 *)src;
    u8 *outBase = (u8 *)dest;
    u16 *out16 = (u16 *)dest;

    if ((*in & 0xF0) != 0x10)
        return;

    u32 uncompressedSize = in[1] | (in[2] << 8) | (in[3] << 16);
    in += 4;

    u32 written = 0;
    u16 halfwordBuf = 0;

    while (written < uncompressedSize)
    {
        u8 flags = *in++;

        for (int i = 0; i < 8 && written < uncompressedSize; i++)
        {
            if (flags & 0x80)
            {
                u8 b1 = *in++;
                u8 b2 = *in++;
                u32 length = (b1 >> 4) + 3;
                u32 disp = (((b1 & 0x0F) << 8) | b2) + 1;

                for (u32 j = 0; j < length && written < uncompressedSize; j++)
                {
                    u8 nextByte = outBase[written - disp];
                    if ((written & 1) == 0)
                        halfwordBuf = nextByte;
                    else
                    {
                        halfwordBuf |= (nextByte << 8);
                        *out16++ = halfwordBuf;
                    }
                    written++;
                }
            }
            else
            {
                u8 nextByte = *in++;
                if ((written & 1) == 0)
                    halfwordBuf = nextByte;
                else
                {
                    halfwordBuf |= (nextByte << 8);
                    *out16++ = halfwordBuf;
                }
                written++;
            }
            flags <<= 1;
        }
    }

    if ((written & 1) != 0)
        *out16 = halfwordBuf;
}

//-----------------------------------------------------------------------
// LZ77UnCompWram (SWI 0x11) — byte-wise, no width restriction.
//-----------------------------------------------------------------------
void LZ77UnCompWram(const void *src, void *dest)
{
    const u8 *in = (const u8 *)src;
    u8 *out = (u8 *)dest;

    if ((*in & 0xF0) != 0x10)
        return;

    u32 uncompressedSize = in[1] | (in[2] << 8) | (in[3] << 16);
    in += 4;

    u32 written = 0;

    while (written < uncompressedSize)
    {
        u8 flags = *in++;

        for (int i = 0; i < 8 && written < uncompressedSize; i++)
        {
            if (flags & 0x80)
            {
                u8 b1 = *in++;
                u8 b2 = *in++;
                u32 length = (b1 >> 4) + 3;
                u32 disp = (((b1 & 0x0F) << 8) | b2) + 1;
                const u8 *copyFrom = out - disp;

                for (u32 j = 0; j < length && written < uncompressedSize; j++)
                {
                    *out++ = *copyFrom++;
                    written++;
                }
            }
            else
            {
                *out++ = *in++;
                written++;
            }
            flags <<= 1;
        }
    }
}

//-----------------------------------------------------------------------
// MultiBoot (SWI 0x25) — no link cable exists on any host target.
// Always reports failure, matching the earlier multiboot.c decision.
//-----------------------------------------------------------------------
int MultiBoot(struct MultiBootParam *mp)
{
    (void)mp;
    return 1;
}

//-----------------------------------------------------------------------
// ObjAffineSet (SWI 0x0F)
//
// VERIFY: field names below (rotation/xScale/yScale) are UNCONFIRMED —
// carried over from the original draft, not yet checked against this
// project's real struct ObjAffineSrcData the way BgAffineSrcData was.
// Paste the real struct and this will be corrected to match.
// Math below already uses the corrected sx*cos()-style formula.
//-----------------------------------------------------------------------
void ObjAffineSet(struct ObjAffineSrcData *src, void *dest, s32 count, s32 offset)
{
    u8 *outBase = (u8 *)dest;

    for (s32 i = 0; i < count; i++)
    {
        u8 angleIdx = (u8)(src[i].rotation >> 8); // VERIFY field name
        double rad = (angleIdx / 256.0) * (2.0 * M_PI);
        double cosVal = cos(rad);
        double sinVal = sin(rad);

        s16 pa = (s16)round(src[i].xScale * cosVal);  // VERIFY field name
        s16 pb = (s16)round(-src[i].xScale * sinVal);
        s16 pc = (s16)round(src[i].yScale * sinVal);   // VERIFY field name
        s16 pd = (s16)round(src[i].yScale * cosVal);

        u8 *out = outBase + (i * 4 * offset);
        *(s16 *)(out + 0 * offset) = pa;
        *(s16 *)(out + 1 * offset) = pb;
        *(s16 *)(out + 2 * offset) = pc;
        *(s16 *)(out + 3 * offset) = pd;
    }
}

//-----------------------------------------------------------------------
// RegisterRamReset (SWI 0x01)
//
// Original used hardcoded GBA physical addresses (0x02000000 etc.) —
// those don't exist on host and would segfault immediately. Stubbed
// to a safe no-op until real host-side EWRAM/IWRAM/VRAM/OAM/PLTT
// buffers exist for this to target instead.
//
// NOTE: RESET_EWRAM / RESET_IWRAM / etc. constant names below are
// assumed to match include/gba/syscall.h — verify with:
//   grep -n "RESET_" include/gba/syscall.h
//-----------------------------------------------------------------------
void RegisterRamReset(u32 resetFlags)
{
    (void)resetFlags;
    // TODO: memset the real host-side memory arrays once they exist.
}

//-----------------------------------------------------------------------
// Sqrt (SWI 0x08)
//-----------------------------------------------------------------------
u16 Sqrt(u32 num)
{
    return (u16)sqrt((double)num);
}

// SoftReset and VBlankIntrWait deliberately NOT included here —
// still real ARM asm in src/libagbsyscall.s, deferred until the
// SDL2 game loop exists (see project notes).