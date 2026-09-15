	.include "constants/gba_constants.inc"
	.include "asm/macros.inc"

@ //-------------------------------------------------------------
@ 	#include "types.h"  //Added here for the data defined for the game logic
@ 	#include <math.h>
@ 	#include <string.h>
@ 	#include "gba/syscall.h"
@ //-------------------------------------------------------------

	.syntax unified

	.text

@ //-------------------------------------------------------------
@ #ifndef M_PI
@ #define M_PI 3.14159265358979323846
@ #endif

@ u16 ArcTan2(s16 x, s16 y)
@ {
@     // Standard atan2 expects y first, then x
@     double rad = atan2((double)y, (double)x);

@     // Normalize negative angles from (-PI, 0] to [PI, 2*PI)
@     if (rad < 0.0)
@     {
@         rad += 2.0 * M_PI;
@     }

@     // Convert radians [0, 2*PI) to GBA binary angle range [0x0000, 0xFFFF]
@     u32 angle = (u32)(rad * (65536.0 / (2.0 * M_PI)));

@     return (u16)(angle & 0xFFFF);
@ }
@ @ 	thumb_func_start ArcTan2
@ @ ArcTan2:
@ @ 	svc 0xA
@ @ 	bx lr
@ @ 	thumb_func_end ArcTan2
@ //-------------------------------------------------------------

@ //-------------------------------------------------------------
@ void BgAffineSet(struct BgAffineSrcData *src, struct BgAffineDstData *dest, s32 count)
@ {
@     for (s32 i = 0; i < count; i++)
@     {
@         // 1. GBA BIOS Quirk: Only the upper 8 bits of rotation are evaluated
@         u8 angleIdx = (u8)(src[i].rotation >> 8);

@         // 2. Convert 8-bit angle (0-255) to radians (0 to 2*PI)
@         double rad = (angleIdx / 256.0) * (2.0 * M_PI);
@         double cosVal = cos(rad);
@         double sinVal = sin(rad);

@         s16 pa = 0, pb = 0, pc = 0, pd = 0;

@         // 3. Compute Q8.8 matrix parameters
@         if (src[i].xScale != 0)
@         {
@             pa = (s16)round((cosVal * 65536.0) / src[i].xScale);
@             pb = (s16)round((sinVal * 65536.0) / src[i].xScale);
@         }

@         if (src[i].yScale != 0)
@         {
@             pc = (s16)round((-sinVal * 65536.0) / src[i].yScale);
@             pd = (s16)round((cosVal * 65536.0) / src[i].yScale);
@         }

@         dest[i].pa = pa;
@         dest[i].pb = pb;
@         dest[i].pc = pc;
@         dest[i].pd = pd;

@         // 4. Compute top-left start coordinates (dx, dy) in Q24.8 fixed point
@         dest[i].dx = src[i].texCenterX - (pa * src[i].dispCenterX + pb * src[i].dispCenterY);
@         dest[i].dy = src[i].texCenterY - (pc * src[i].dispCenterX + pd * src[i].dispCenterY);
@     }
@ }

@ @ 	thumb_func_start BgAffineSet
@ @ BgAffineSet:
@ @ 	svc 0xE
@ @ 	bx lr
@ @ 	thumb_func_end BgAffineSet
@ //-------------------------------------------------------------

@ //-------------------------------------------------------------
@ void CpuFastSet(const void *src, void *dest, u32 control)
@ {
@     // 1. Extract the raw word count (bits 0-20)
@     u32 words = control & 0x1FFFFF;
@     if (words == 0) return;

@     // 2. Replicate GBA hardware quirk: Round up word count to a multiple of 8 words (32 bytes)
@     u32 roundedWords = (words + 7) & ~(u32)7;

@     // 3. Check Bit 24 (CPU_FAST_SET_SRC_FIXED / 0x01000000) for Copy vs Fill mode
@     if (control & CPU_FAST_SET_SRC_FIXED)
@     {
@         // FILL MODE: Repeatedly write the single 32-bit value at *src into dest
@         u32 fillValue = *(const u32 *)src;
@         u32 *d = (u32 *)dest;

@         for (u32 i = 0; i < roundedWords; i++)
@         {
@             d[i] = fillValue;
@         }
@     }
@     else
@     {
@         // COPY MODE: Copy memory in 32-bit word chunks
@         const u32 *s = (const u32 *)src;
@         u32 *d = (u32 *)dest;

@         for (u32 i = 0; i < roundedWords; i++)
@         {
@             d[i] = s[i];
@         }
@     }
@ }

@ @ 	thumb_func_start CpuFastSet
@ @ CpuFastSet:
@ @ 	svc 0xC
@ @ 	bx lr
@ @ 	thumb_func_end CpuFastSet
@ //-------------------------------------------------------------

@ //-------------------------------------------------------------
@ void CpuSet(const void *src, void *dest, u32 control)
@ {
@     u32 count = control & 0x1FFFFF;
@     if (count == 0) return;

@     int is32Bit = (control & CPU_SET_32BIT) != 0;    // Bit 26 check
@     int isFixed = (control & CPU_SET_SRC_FIXED) != 0;  // Bit 24 check

@     if (is32Bit)
@     {
@         u32 *d = (u32 *)dest;
@         const u32 *s = (const u32 *)src;

@         if (isFixed)
@         {
@             // 32-bit Fill Mode
@             u32 fillVal = *s;
@             for (u32 i = 0; i < count; i++)
@             {
@                 d[i] = fillVal;
@             }
@         }
@         else
@         {
@             // 32-bit Copy Mode
@             for (u32 i = 0; i < count; i++)
@             {
@                 d[i] = s[i];
@             }
@         }
@     }
@     else
@     {
@         u16 *d = (u16 *)dest;
@         const u16 *s = (const u16 *)src;

@         if (isFixed)
@         {
@             // 16-bit Fill Mode
@             u16 fillVal = *s;
@             for (u32 i = 0; i < count; i++)
@             {
@                 d[i] = fillVal;
@             }
@         }
@         else
@         {
@             // 16-bit Copy Mode
@             for (u32 i = 0; i < count; i++)
@             {
@                 d[i] = s[i];
@             }
@         }
@     }
@ }

@ @ 	thumb_func_start CpuSet
@ @ CpuSet:
@ @ 	svc 0xB
@ @ 	bx lr
@ @ 	thumb_func_end CpuSet
@ //-------------------------------------------------------------

@ //-------------------------------------------------------------
@ s32 Div(s32 num, s32 denom)
@ {
@ 	if(denom == 0)
@ 	{
@ 		while(1);
@ 	}
@ 	else
@ 	{
@ 		return num/denom;
@ 	}
@ }

@ @ 	thumb_func_start Div
@ @ Div:
@ @ 	svc 0x6
@ @ 	bx lr
@ @ 	thumb_func_end Div
@ //-------------------------------------------------------------

@ //-------------------------------------------------------------
@ // 16-Bit Halfword Decompression for VRAM
@ void LZ77UnCompVram(const void *src, void *dest)
@ {
@     const u8 *in = (const u8 *)src;
@     u8 *outBase = (u8 *)dest;
@     u16 *out16 = (u16 *)dest;

@     if ((*in & 0xF0) != 0x10)
@         return;

@     u32 uncompressedSize = in[1] | (in[2] << 8) | (in[3] << 16);
@     in += 4;

@     u32 written = 0;
@     u16 halfwordBuf = 0;

@     while (written < uncompressedSize)
@     {
@         u8 flags = *in++;

@         for (int i = 0; i < 8 && written < uncompressedSize; i++)
@         {
@             if (flags & 0x80)
@             {
@                 u8 b1 = *in++;
@                 u8 b2 = *in++;

@                 u32 length = (b1 >> 4) + 3;
@                 u32 disp = (((b1 & 0x0F) << 8) | b2) + 1;

@                 for (u32 j = 0; j < length && written < uncompressedSize; j++)
@                 {
@                     u8 nextByte = outBase[written - disp];

@                     // Buffer into 16-bit halfwords before writing to dest
@                     if ((written & 1) == 0)
@                     {
@                         halfwordBuf = nextByte;
@                     }
@                     else
@                     {
@                         halfwordBuf |= (nextByte << 8);
@                         *out16++ = halfwordBuf;
@                     }
@                     written++;
@                 }
@             }
@             else
@             {
@                 u8 nextByte = *in++;

@                 if ((written & 1) == 0)
@                 {
@                     halfwordBuf = nextByte;
@                 }
@                 else
@                 {
@                     halfwordBuf |= (nextByte << 8);
@                     *out16++ = halfwordBuf;
@                 }
@                 written++;
@             }

@             flags <<= 1;
@         }
@     }

@     // Flush remaining odd byte if uncompressed length was not even
@     if ((written & 1) != 0)
@     {
@         *out16 = halfwordBuf;
@     }
@ }

@ @ 	thumb_func_start LZ77UnCompVram
@ @ LZ77UnCompVram:
@ @ 	svc 0x12
@ @ 	bx lr
@ @ 	thumb_func_end LZ77UnCompVram
@ //-------------------------------------------------------------

@ //-------------------------------------------------------------
@ // 8-Bit Decompression for WRAM
@ void LZ77UnCompWram(const void *src, void *dest)
@ {
@     const u8 *in = (const u8 *)src;
@     u8 *out = (u8 *)dest;

@     // Verify LZ77 header (Bits 4-7 of Byte 0 must equal 1)
@     if ((*in & 0xF0) != 0x10)
@         return;

@     // Extract 24-bit decompressed length (Bytes 1-3)
@     u32 uncompressedSize = in[1] | (in[2] << 8) | (in[3] << 16);
@     in += 4;

@     u32 written = 0;

@     while (written < uncompressedSize)
@     {
@         u8 flags = *in++;

@         for (int i = 0; i < 8 && written < uncompressedSize; i++)
@         {
@             if (flags & 0x80)
@             {
@                 // Compressed Block: 2 bytes
@                 u8 b1 = *in++;
@                 u8 b2 = *in++;

@                 u32 length = (b1 >> 4) + 3;
@                 u32 disp = (((b1 & 0x0F) << 8) | b2) + 1;

@                 const u8 *copyFrom = out - disp;

@                 for (u32 j = 0; j < length && written < uncompressedSize; j++)
@                 {
@                     *out++ = *copyFrom++;
@                     written++;
@                 }
@             }
@             else
@             {
@                 // Uncompressed Literal Byte
@                 *out++ = *in++;
@                 written++;
@             }

@             flags <<= 1;
@         }
@     }
@ }

@ @ 	thumb_func_start LZ77UnCompWram
@ @ LZ77UnCompWram:
@ @ 	svc 0x11
@ @ 	bx lr
@ @ 	thumb_func_end LZ77UnCompWram
@ //-------------------------------------------------------------

@ //-------------------------------------------------------------
@ int MultiBoot(struct MultiBootParam *mp)
@ {
@     // Silence compiler warning for unused parameter
@     (void)mp;

@     // Return 1 (failed) to indicate no slave GBA consoles are connected via serial cable.
@     return 1;
@ }

@ @ 	thumb_func_start MultiBoot
@ @ MultiBoot:
@ @ 	movs r1, 0x1
@ @ 	svc 0x25
@ @ 	bx lr
@ @ 	thumb_func_end MultiBoot
@ //-------------------------------------------------------------

@ //-------------------------------------------------------------
@ void ObjAffineSet(const struct ObjAffineSrcData *src, void *dest, s32 count, s32 offset)
@ {
@     u8 *outBase = (u8 *)dest;

@     for (s32 i = 0; i < count; i++)
@     {
@         // 1. GBA BIOS Quirk: Only the upper 8 bits of rotation are used
@         u8 angleIdx = (u8)(src[i].rotation >> 8);

@         // 2. Convert 8-bit angle (0-255) to radians (0 to 2*PI)
@         double rad = (angleIdx / 256.0) * (2.0 * M_PI);
@         double cosVal = cos(rad);
@         double sinVal = sin(rad);

@         s16 pa = 0, pb = 0, pc = 0, pd = 0;

@         // 3. Compute Q8.8 fixed-point matrix parameters with zero-check guards
@         if (src[i].xScale != 0)
@         {
@             pa = (s16)round((cosVal * 65536.0) / src[i].xScale);
@             pb = (s16)round((sinVal * 65536.0) / src[i].xScale);
@         }

@         if (src[i].yScale != 0)
@         {
@             pc = (s16)round((-sinVal * 65536.0) / src[i].yScale);
@             pd = (s16)round((cosVal * 65536.0) / src[i].yScale);
@         }

@         // 4. Write parameters using byte offset stride (handles OAM and packed memory)
@         u8 *out = outBase + (i * 4 * offset);

@         *(s16 *)(out + 0 * offset) = pa;
@         *(s16 *)(out + 1 * offset) = pb;
@         *(s16 *)(out + 2 * offset) = pc;
@         *(s16 *)(out + 3 * offset) = pd;
@     }
@ }

@ @ 	thumb_func_start ObjAffineSet
@ @ ObjAffineSet:
@ @ 	svc 0xF
@ @ 	bx lr
@ @ 	thumb_func_end ObjAffineSet
@ //-------------------------------------------------------------

@ //-------------------------------------------------------------

@ // Defined GBA Memory Base Addresses / Sizes
@ #define EWRAM_ADDR   ((void *)0x02000000)
@ #define EWRAM_SIZE   (256 * 1024)

@ #define IWRAM_ADDR   ((void *)0x03000000)
@ #define IWRAM_SIZE   (32 * 1024 - 0x200) // Excludes top 0x200 bytes (stack/BIOS vars)

@ #define PALETTE_ADDR ((void *)0x05000000)
@ #define PALETTE_SIZE (1 * 1024)

@ #define VRAM_ADDR    ((void *)0x06000000)
@ #define VRAM_SIZE    (96 * 1024)

@ #define OAM_ADDR     ((void *)0x07000000)
@ #define OAM_SIZE     (1 * 1024)

@ void RegisterRamReset(u32 resetFlags)
@ {
@     // 1. Clear External WRAM (256KB)
@     if (resetFlags & RESET_EWRAM)
@     {
@         memset(EWRAM_ADDR, 0, EWRAM_SIZE);
@     }

@     // 2. Clear Internal WRAM (32KB minus top 512 bytes)
@     if (resetFlags & RESET_IWRAM)
@     {
@         memset(IWRAM_ADDR, 0, IWRAM_SIZE);
@     }

@     // 3. Clear Palette RAM (1KB)
@     if (resetFlags & RESET_PALETTE)
@     {
@         memset(PALETTE_ADDR, 0, PALETTE_SIZE);
@     }

@     // 4. Clear Video RAM (96KB)
@     if (resetFlags & RESET_VRAM)
@     {
@         memset(VRAM_ADDR, 0, VRAM_SIZE);
@     }

@     // 5. Clear Sprite OAM Memory (1KB)
@     if (resetFlags & RESET_OAM)
@     {
@         memset(OAM_ADDR, 0, OAM_SIZE);
@     }

@     // 6. Reset Hardware Registers (SIO, Sound, Display)
@     if (resetFlags & (RESET_SIO_REGS | RESET_SOUND_REGS | RESET_REGS))
@     {
@         // If your x86 port uses an emulation layer or struct for I/O registers,
@         // clear or reset those structs here.
@         // E.g., REG_DISPCNT = 0x0080; (Forced Blank)
@     }
@ }
@ @ 	thumb_func_start RegisterRamReset
@ @ RegisterRamReset:
@ @ 	svc 0x1
@ @ 	bx lr
@ @ 	thumb_func_end RegisterRamReset
@ //-------------------------------------------------------------

	thumb_func_start SoftReset
SoftReset:
	ldr r3, =REG_IME
	movs r2, 0
	strb r2, [r3]
	ldr r1, =0x03007f00 @ User Stack
	mov sp, r1
	svc 0x1
	svc 0
	.pool
	thumb_func_end SoftReset

@ //-------------------------------------------------------------

@ u16 Sqrt(u32 num)
@ {
@ 	return (u16)sqrt((double)num);
@ }

@ @ 	thumb_func_start Sqrt
@ @ Sqrt:
@ @ 	svc 0x8
@ @ 	bx lr
@ @ 	thumb_func_end Sqrt
@ //-------------------------------------------------------------

	thumb_func_start VBlankIntrWait
VBlankIntrWait:
	movs r2, 0
	svc 0x5
	bx lr
	thumb_func_end VBlankIntrWait

	.align 2, 0 @ Don't pad with nop.
