// Host emulation of GBA DMA.
//  * "Start immediately" transfers run right away inside Host_DmaSet().
//  * HBlank-timed transfers (scanline effects: battle intro ground, battle
//    transitions, cave flash) are ARMED here and executed one unit-group per
//    visible scanline by Host_HBlankDmaStep(), which the compositor calls at
//    the end of every scanline. DmaStop() is a macro that just clears the
//    enable bit in the (host memory) DMA control register, so we mirror the
//    control word into that register when arming and re-check it every line.
//  * VBlank / special timed DMA (sound FIFO) is still not emulated.
#include <stdint.h>
#include "gba/types.h"
#include "gba/defines.h"
#include "gba/io_reg.h"
#include "gba_memory_host.h"

#define DMA_CTL_REG(n) (((volatile uint16_t *)(REG_ADDR_DMA0 + 12 * (n)))[5])

struct HBlankDma
{
    int active;
    uint32_t src, dest, destStart, count;
    int is32, srcStep, dstStep, dstReload, repeat;
};
static struct HBlankDma sHb[4];

static void Transfer(uint32_t *pSrc, uint32_t *pDst, uint32_t count, int is32, int srcStep, int dstStep)
{
    uint8_t *d = (uint8_t *)(uintptr_t)*pDst;
    const uint8_t *s = (const uint8_t *)(uintptr_t)*pSrc;
    for (uint32_t i = 0; i < count; i++, d += dstStep, s += srcStep)
    {
        if (is32) *(volatile uint32_t *)d = *(const volatile uint32_t *)s;
        else      *(volatile uint16_t *)d = *(const volatile uint16_t *)s;
    }
    *pDst = (uint32_t)(uintptr_t)d;
    *pSrc = (uint32_t)(uintptr_t)s;
}

void Host_DmaSet(int dmaNum, unsigned int src, unsigned int dest, unsigned int control)
{
    uint32_t count = control & 0xFFFF;
    uint32_t cnt = control >> 16;
    int timing = (cnt >> 12) & 3;      // 0 now, 1 VBlank, 2 HBlank, 3 special

    if (!(cnt & 0x8000))               // enable bit not set: also disarms the channel
    {
        if (dmaNum >= 0 && dmaNum < 4) sHb[dmaNum].active = 0;
        return;
    }

    if (count == 0)
        count = (dmaNum == 3) ? 0x10000 : 0x4000;

    int is32   = (cnt >> 10) & 1;
    int dstCtl = (cnt >> 5) & 3;       // 0 inc, 1 dec, 2 fixed, 3 inc+reload
    int srcCtl = (cnt >> 7) & 3;       // 0 inc, 1 dec, 2 fixed
    int step   = is32 ? 4 : 2;
    int dstStep = (dstCtl == 1) ? -step : (dstCtl == 2) ? 0 : step;
    int srcStep = (srcCtl == 1) ? -step : (srcCtl == 2) ? 0 : step;

    if (timing == 2 && dmaNum >= 0 && dmaNum < 4)      // HBlank: arm it
    {
        struct HBlankDma *h = &sHb[dmaNum];
        h->active = 1;
        h->src = src;
        h->dest = h->destStart = dest;
        h->count = count;
        h->is32 = is32;
        h->srcStep = srcStep;
        h->dstStep = dstStep;
        h->dstReload = (dstCtl == 3);
        h->repeat = (cnt >> 9) & 1;
        DMA_CTL_REG(dmaNum) = (uint16_t)cnt;           // so DmaStop() can clear bit 15
        return;
    }
    if (timing != 0)                                    // VBlank/special: not emulated
        return;

    uint32_t s = src, d = dest;
    Transfer(&s, &d, count, is32, srcStep, dstStep);
}

// Called by the compositor in the HBlank that follows each visible scanline.
void Host_HBlankDmaStep(void)
{
    for (int n = 0; n < 4; n++)
    {
        struct HBlankDma *h = &sHb[n];
        if (!h->active) continue;
        if (!(DMA_CTL_REG(n) & 0x8000))                 // DmaStop() was called
        {
            h->active = 0;
            continue;
        }
        Transfer(&h->src, &h->dest, h->count, h->is32, h->srcStep, h->dstStep);
        if (h->dstReload) h->dest = h->destStart;
        if (!h->repeat)
        {
            h->active = 0;
            DMA_CTL_REG(n) &= (uint16_t)~0x8000;
        }
    }
}
