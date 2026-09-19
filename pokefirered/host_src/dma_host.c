// Host emulation of GBA DMA. On hardware, writing the DMA control register
// starts the transfer; here DmaSet() calls this and copies immediately.
// Only "start immediately" transfers are emulated. HBlank/VBlank/special
// timed DMA (scanline effects, sound FIFO) is skipped for now.
#include <stdint.h>

void Host_DmaSet(int dmaNum, unsigned int src, unsigned int dest, unsigned int control)
{
    uint32_t count = control & 0xFFFF;
    uint32_t cnt = control >> 16;

    if (!(cnt & 0x8000))            // enable bit not set
        return;
    if (((cnt >> 12) & 3) != 0)     // non-immediate start timing: not emulated
        return;

    if (count == 0)
        count = (dmaNum == 3) ? 0x10000 : 0x4000;

    int is32   = (cnt >> 10) & 1;
    int dstCtl = (cnt >> 5) & 3;    // 0 inc, 1 dec, 2 fixed, 3 inc+reload
    int srcCtl = (cnt >> 7) & 3;    // 0 inc, 1 dec, 2 fixed
    int step   = is32 ? 4 : 2;
    int dstStep = (dstCtl == 1) ? -step : (dstCtl == 2) ? 0 : step;
    int srcStep = (srcCtl == 1) ? -step : (srcCtl == 2) ? 0 : step;

    uint8_t *d = (uint8_t *)(uintptr_t)dest;
    const uint8_t *s = (const uint8_t *)(uintptr_t)src;
    uint32_t i;

    if (is32)
    {
        for (i = 0; i < count; i++, d += dstStep, s += srcStep)
            *(volatile uint32_t *)d = *(const volatile uint32_t *)s;
    }
    else
    {
        for (i = 0; i < count; i++, d += dstStep, s += srcStep)
            *(volatile uint16_t *)d = *(const volatile uint16_t *)s;
    }
}
