#ifdef HOST_BUILD

// Scanline compositor: BG0-3 (text mode), OBJ (normal + affine), real
// priority ordering, WIN0/WIN1/OBJ-window, alpha + brightness blending.
// Not handled yet: affine BGs (mode 1/2 BG2/BG3), bitmap modes, mosaic,
// per-scanline register changes (HBlank effects).

#include <string.h>
#include <stdio.h>
#include "gba/types.h"
#include "gba/defines.h"
#include "gba/io_reg.h"
#include "main.h"
#include "gba_memory_host.h"

extern u8 gGbaVram[];
extern u8 gGbaOam[];

#define SCR_W 240
#define SCR_H 160

#define DC_FORCED_BLANK 0x0080
#define DC_OBJ_1D       0x0040
#define DC_OBJ_ON       0x1000
#define DC_WIN0_ON      0x2000
#define DC_WIN1_ON      0x4000
#define DC_OBJWIN_ON    0x8000

static s32 sBg[4][SCR_W];
static s32 sObj[SCR_W];
static u8  sObjPrio[SCR_W];
static u8  sObjSemi[SCR_W];
static u8  sObjWin[SCR_W];
static u8  sWinMask[SCR_W];

static const struct { u8 w, h; } sObjDim[4][4] =
{
    {{8,8},  {16,16}, {32,32}, {64,64}},
    {{16,8}, {32,8},  {32,16}, {64,32}},
    {{8,16}, {8,32},  {16,32}, {32,64}},
    {{8,8},  {8,8},   {8,8},   {8,8}}
};

static void BuildBgLine(int bg, int y, u16 cnt, u16 hofs, u16 vofs)
{
    const u16 *pltt = (const u16 *)gGbaPltt;
    u32 charBase = ((cnt >> 2) & 3) * 0x4000;
    u32 mapBase  = ((cnt >> 8) & 31) * 0x800;
    int sizeBits = (cnt >> 14) & 3;
    int is8bpp = (cnt & 0x80) != 0;
    int mapW = (sizeBits & 1) ? 64 : 32;
    int mapH = (sizeBits & 2) ? 64 : 32;
    int bgY = (y + vofs) & (mapH * 8 - 1);
    int ty = bgY >> 3, py = bgY & 7;

    for (int x = 0; x < SCR_W; x++)
    {
        int bgX = (x + hofs) & (mapW * 8 - 1);
        int tx = bgX >> 3, px = bgX & 7;
        int block = (ty >> 5) * (mapW >> 5) + (tx >> 5);
        u32 entryAddr = mapBase + block * 0x800 + (((ty & 31) * 32) + (tx & 31)) * 2;
        u16 entry = *(u16 *)&gGbaVram[entryAddr];
        u16 tileId = entry & 0x3FF;
        int sx = (entry & 0x400) ? 7 - px : px;
        int sy = (entry & 0x800) ? 7 - py : py;
        int bank = (entry >> 12) & 0xF;

        sBg[bg][x] = -1;
        if (!is8bpp)
        {
            u8 b = gGbaVram[charBase + tileId * 32 + sy * 4 + (sx >> 1)];
            int idx = (sx & 1) ? (b >> 4) : (b & 0xF);
            if (idx) sBg[bg][x] = pltt[bank * 16 + idx] & 0x7FFF;
        }
        else
        {
            int idx = gGbaVram[charBase + tileId * 64 + sy * 8 + sx];
            if (idx) sBg[bg][x] = pltt[idx] & 0x7FFF;
        }
    }
}

static void BuildAffineBgLine(int bg, int y, u16 cnt)
{
    const u16 *pltt = (const u16 *)gGbaPltt;
    u32 charBase = ((cnt >> 2) & 3) * 0x4000;
    u32 mapBase  = ((cnt >> 8) & 31) * 0x800;
    int sizePx = 128 << ((cnt >> 14) & 3);
    int wrap = (cnt >> 13) & 1;
    s32 pa, pb, pc, pd, rx, ry;

    if (bg == 2)
    {
        pa = (s16)REG_BG2PA; pb = (s16)REG_BG2PB; pc = (s16)REG_BG2PC; pd = (s16)REG_BG2PD;
        rx = (s32)REG_BG2X;  ry = (s32)REG_BG2Y;
    }
    else
    {
        pa = (s16)REG_BG3PA; pb = (s16)REG_BG3PB; pc = (s16)REG_BG3PC; pd = (s16)REG_BG3PD;
        rx = (s32)REG_BG3X;  ry = (s32)REG_BG3Y;
    }
    rx = (s32)((u32)rx << 4) >> 4;   // sign-extend 28-bit reference point
    ry = (s32)((u32)ry << 4) >> 4;
    rx += pb * y;
    ry += pd * y;

    for (int x = 0; x < SCR_W; x++)
    {
        int tx = (rx + pa * x) >> 8;
        int ty = (ry + pc * x) >> 8;
        sBg[bg][x] = -1;
        if (wrap) { tx &= sizePx - 1; ty &= sizePx - 1; }
        else if (tx < 0 || ty < 0 || tx >= sizePx || ty >= sizePx) continue;
        int tile = gGbaVram[mapBase + (ty >> 3) * (sizePx >> 3) + (tx >> 3)];
        int idx = gGbaVram[charBase + tile * 64 + (ty & 7) * 8 + (tx & 7)];
        if (idx) sBg[bg][x] = pltt[idx] & 0x7FFF;
    }
}

static void BuildObjLine(int y, int is1D)
{
    const u16 *pltt = (const u16 *)gGbaPltt;

    for (int x = 0; x < SCR_W; x++)
    {
        sObj[x] = -1;
        sObjPrio[x] = 4;
        sObjSemi[x] = 0;
        sObjWin[x] = 0;
    }

    for (int i = 0; i < 128; i++)
    {
        u16 a0 = *(u16 *)&gGbaOam[i * 8 + 0];
        u16 a1 = *(u16 *)&gGbaOam[i * 8 + 2];
        u16 a2 = *(u16 *)&gGbaOam[i * 8 + 4];

        int affine = (a0 >> 8) & 1;
        int bit9 = (a0 >> 9) & 1;             // disabled (normal) / double-size (affine)
        if (!affine && bit9) continue;
        int objMode = (a0 >> 10) & 3;         // 0 normal, 1 semi-transparent, 2 obj-window
        if (objMode == 3) continue;
        int is8 = (a0 >> 13) & 1;
        int shape = (a0 >> 14) & 3;
        int size = (a1 >> 14) & 3;
        int w = sObjDim[shape][size].w;
        int h = sObjDim[shape][size].h;
        int boxW = (affine && bit9) ? w * 2 : w;
        int boxH = (affine && bit9) ? h * 2 : h;

        int oy = a0 & 0xFF;
        int rel = (y - oy) & 0xFF;
        if (rel >= boxH) continue;

        int ox = a1 & 0x1FF;
        if (ox >= 256) ox -= 512;

        int prio = (a2 >> 10) & 3;
        int tileId = a2 & 0x3FF;
        int bank = (a2 >> 12) & 0xF;

        s32 pa = 0, pb = 0, pc = 0, pd = 0;
        if (affine)
        {
            int grp = (a1 >> 9) & 0x1F;
            pa = *(s16 *)&gGbaOam[grp * 32 + 6];
            pb = *(s16 *)&gGbaOam[grp * 32 + 14];
            pc = *(s16 *)&gGbaOam[grp * 32 + 22];
            pd = *(s16 *)&gGbaOam[grp * 32 + 30];
        }

        for (int px = 0; px < boxW; px++)
        {
            int sx = ox + px;
            if (sx < 0 || sx >= SCR_W) continue;

            int tx, ty;
            if (affine)
            {
                int dx = px - boxW / 2;
                int dy = rel - boxH / 2;
                tx = ((pa * dx + pb * dy) >> 8) + w / 2;
                ty = ((pc * dx + pd * dy) >> 8) + h / 2;
                if (tx < 0 || tx >= w || ty < 0 || ty >= h) continue;
            }
            else
            {
                tx = (a1 & 0x1000) ? w - 1 - px : px;
                ty = (a1 & 0x2000) ? h - 1 - rel : rel;
            }

            int idx;
            int tileNum;
            u32 off;
            if (!is8)
            {
                tileNum = (tileId + (is1D ? (ty >> 3) * (w >> 3) + (tx >> 3)
                                          : (ty >> 3) * 32 + (tx >> 3))) & 0x3FF;
                off = (tileNum * 32 + (ty & 7) * 4 + ((tx & 7) >> 1)) & 0x7FFF;
                u8 b = gGbaVram[0x10000 + off];
                idx = (tx & 1) ? (b >> 4) : (b & 0xF);
            }
            else
            {
                tileNum = (tileId + (is1D ? ((ty >> 3) * (w >> 3) + (tx >> 3)) * 2
                                          : (ty >> 3) * 32 + (tx >> 3) * 2)) & 0x3FF;
                off = (tileNum * 32 + (ty & 7) * 8 + (tx & 7)) & 0x7FFF;
                idx = gGbaVram[0x10000 + off];
            }
            if (idx == 0) continue;

            if (objMode == 2) { sObjWin[sx] = 1; continue; }
            if (sObj[sx] >= 0 && sObjPrio[sx] <= prio) continue;

            u16 color = is8 ? pltt[256 + idx] : pltt[256 + bank * 16 + idx];
            sObj[sx] = color & 0x7FFF;
            sObjPrio[sx] = (u8)prio;
            sObjSemi[sx] = (objMode == 1);
        }
    }
}

static void ApplyWindow(int y, u16 h, u16 v, u8 flags)
{
    int x1 = h >> 8, x2 = h & 0xFF;
    int y1 = v >> 8, y2 = v & 0xFF;
    if (x2 > SCR_W || x1 > x2) x2 = SCR_W;
    if (y2 > SCR_H || y1 > y2) y2 = SCR_H;
    if (y < y1 || y >= y2) return;
    for (int x = x1; x < x2; x++)
        sWinMask[x] = flags;
}

static void BuildWinMask(int y, u16 dispcnt)
{
    if (!(dispcnt & (DC_WIN0_ON | DC_WIN1_ON | DC_OBJWIN_ON)))
    {
        memset(sWinMask, 0x3F, sizeof(sWinMask));
        return;
    }
    u16 winin = REG_WININ, winout = REG_WINOUT;
    for (int x = 0; x < SCR_W; x++)
        sWinMask[x] = winout & 0x3F;
    if (dispcnt & DC_OBJWIN_ON)
        for (int x = 0; x < SCR_W; x++)
            if (sObjWin[x]) sWinMask[x] = (winout >> 8) & 0x3F;
    if (dispcnt & DC_WIN1_ON) ApplyWindow(y, REG_WIN1H, REG_WIN1V, (winin >> 8) & 0x3F);
    if (dispcnt & DC_WIN0_ON) ApplyWindow(y, REG_WIN0H, REG_WIN0V, winin & 0x3F);
}

static u16 BlendColors(u16 a, u16 b, int eva, int evb)
{
    int r = (((a & 31) * eva) + ((b & 31) * evb)) >> 4;
    int g = ((((a >> 5) & 31) * eva) + (((b >> 5) & 31) * evb)) >> 4;
    int bl = ((((a >> 10) & 31) * eva) + (((b >> 10) & 31) * evb)) >> 4;
    if (r > 31) r = 31;
    if (g > 31) g = 31;
    if (bl > 31) bl = 31;
    return (u16)(r | (g << 5) | (bl << 10));
}

static u16 Brighten(u16 c, int evy)
{
    int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    r += ((31 - r) * evy) >> 4;
    g += ((31 - g) * evy) >> 4;
    b += ((31 - b) * evy) >> 4;
    return (u16)(r | (g << 5) | (b << 10));
}

static u16 Darken(u16 c, int evy)
{
    int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    r -= (r * evy) >> 4;
    g -= (g * evy) >> 4;
    b -= (b * evy) >> 4;
    return (u16)(r | (g << 5) | (b << 10));
}

static u32 ToRgba(u16 c)
{
    u32 r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    r = (r << 3) | (r >> 2);
    g = (g << 3) | (g >> 2);
    b = (b << 3) | (b >> 2);
    return (r << 24) | (g << 16) | (b << 8) | 0xFF;
}

void Host_ComposeFrame(void *pixels, int pitch)
{
    const u16 *pltt = (const u16 *)gGbaPltt;
    u16 dispcnt = REG_DISPCNT;
    int mode = dispcnt & 7;

    {
        static unsigned sDiag = 0;
        if (++sDiag % 120 == 0)
            fprintf(stderr, "[win] WININ=%04X WINOUT=%04X WIN0H=%04X WIN0V=%04X WIN1H=%04X WIN1V=%04X BLDCNT=%04X BLDALPHA=%04X BLDY=%04X\n",
                    REG_WININ, REG_WINOUT, REG_WIN0H, REG_WIN0V, REG_WIN1H, REG_WIN1V, REG_BLDCNT, REG_BLDALPHA, REG_BLDY);
    }
    {
        static unsigned sDiag2 = 0;
        if (++sDiag2 % 120 == 0)
        {
            u16 dcnt[4] = { REG_BG0CNT, REG_BG1CNT, REG_BG2CNT, REG_BG3CNT };
            for (int b = 0; b < 4; b++)
            {
                if (!((dispcnt >> (8 + b)) & 1)) continue;
                u32 cb = ((dcnt[b] >> 2) & 3) * 0x4000;
                u32 mb = ((dcnt[b] >> 8) & 31) * 0x800;
                u32 tsz = (dcnt[b] & 0x80) ? 64 : 32;
                int nz = 0, px = 0;
                for (int i = 0; i < 1024; i++)
                {
                    u16 e = *(u16 *)&gGbaVram[mb + i * 2];
                    u32 t = e & 0x3FF;
                    int any = 0;
                    if (e) nz++;
                    for (u32 k = 0; k < tsz && !any; k++)
                        if (gGbaVram[(cb + t * tsz + k) % 0x18000]) any = 1;
                    px += any;
                }
                fprintf(stderr, "[bg%d] cnt=%04X map=0x%05X char=0x%05X mapEntriesNonZero=%d refsTileWithPixels=%d\n",
                        b, dcnt[b], mb, cb, nz, px);
            }
        }
    }

    if (dispcnt & DC_FORCED_BLANK)
    {
        for (int y = 0; y < SCR_H; y++)
        {
            u32 *row = (u32 *)((u8 *)pixels + y * pitch);
            for (int x = 0; x < SCR_W; x++) row[x] = 0xFFFFFFFF;
        }
        return;
    }

    u16 bgCnt[4]  = { REG_BG0CNT, REG_BG1CNT, REG_BG2CNT, REG_BG3CNT };
    u16 bgHofs[4] = { REG_BG0HOFS, REG_BG1HOFS, REG_BG2HOFS, REG_BG3HOFS };
    u16 bgVofs[4] = { REG_BG0VOFS, REG_BG1VOFS, REG_BG2VOFS, REG_BG3VOFS };

    int bgActive[4];
    int bgAffine[4];
    for (int bg = 0; bg < 4; bg++)
    {
        int on = (dispcnt >> (8 + bg)) & 1;
        int text = (mode == 0) || (mode == 1 && bg < 2);
        int aff = (mode == 1 && bg == 2) || (mode == 2 && bg >= 2);
        bgActive[bg] = on && (text || aff);
        bgAffine[bg] = aff;
    }

    int objOn = (dispcnt & DC_OBJ_ON) != 0;
    int is1D = (dispcnt & DC_OBJ_1D) != 0;

    u16 bldcnt = REG_BLDCNT;
    u16 bldalpha = REG_BLDALPHA;
    u16 bldy = REG_BLDY;
    int bmode = (bldcnt >> 6) & 3;
    int eva = bldalpha & 0x1F;        if (eva > 16) eva = 16;
    int evb = (bldalpha >> 8) & 0x1F; if (evb > 16) evb = 16;
    int evy = bldy & 0x1F;            if (evy > 16) evy = 16;
    u16 backdrop = pltt[0] & 0x7FFF;

    for (int y = 0; y < SCR_H; y++)
    {
        for (int bg = 0; bg < 4; bg++)
        {
            if (!bgActive[bg]) continue;
            if (bgAffine[bg]) BuildAffineBgLine(bg, y, bgCnt[bg]);
            else BuildBgLine(bg, y, bgCnt[bg], bgHofs[bg] & 0x1FF, bgVofs[bg] & 0x1FF);
        }

        if (objOn) BuildObjLine(y, is1D);
        else
            for (int x = 0; x < SCR_W; x++) { sObj[x] = -1; sObjWin[x] = 0; sObjSemi[x] = 0; sObjPrio[x] = 4; }

        BuildWinMask(y, dispcnt);

        u32 *row = (u32 *)((u8 *)pixels + y * pitch);
        for (int x = 0; x < SCR_W; x++)
        {
            u8 mask = sWinMask[x];
            int n = 0;
            int lid[2];
            u16 lc[2];

            for (int prio = 0; prio < 4 && n < 2; prio++)
            {
                if ((mask & 0x10) && objOn && sObj[x] >= 0 && sObjPrio[x] == prio)
                {
                    lid[n] = 4; lc[n] = (u16)sObj[x]; n++;
                }
                for (int bg = 0; bg < 4 && n < 2; bg++)
                {
                    if (!bgActive[bg] || !(mask & (1 << bg))) continue;
                    if (sBg[bg][x] < 0 || (bgCnt[bg] & 3) != prio) continue;
                    lid[n] = bg; lc[n] = (u16)sBg[bg][x]; n++;
                }
            }
            while (n < 2) { lid[n] = 5; lc[n] = backdrop; n++; }

            u16 out = lc[0];
            if (mask & 0x20)
            {
                int firstOK  = (bldcnt >> lid[0]) & 1;
                int secondOK = (bldcnt >> (8 + lid[1])) & 1;
                if (lid[0] == 4 && sObjSemi[x] && secondOK)
                    out = BlendColors(lc[0], lc[1], eva, evb);
                else if (bmode == 1 && firstOK && secondOK)
                    out = BlendColors(lc[0], lc[1], eva, evb);
                else if (bmode == 2 && firstOK)
                    out = Brighten(lc[0], evy);
                else if (bmode == 3 && firstOK)
                    out = Darken(lc[0], evy);
            }
            row[x] = ToRgba(out);
        }
    }
}

#endif // HOST_BUILD
