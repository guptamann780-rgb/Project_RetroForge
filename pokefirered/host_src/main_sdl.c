#ifdef HOST_BUILD

#include <SDL2/SDL.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include "gba/types.h"
#include "gba/defines.h"
#include "gba/io_reg.h"
#include "main.h"
#include "gba_memory_host.h"

#define HOST_SCALE 3
#define GBA_REFRESH_HZ (59.7275)

extern u8 gGbaVram[];
extern u8 gGbaOam[];

static SDL_Window *sWindow;
static SDL_Renderer *sRenderer;
static SDL_Texture *sTexture;
static volatile int sScreenshotRequested = 0;

// Default host keyboard mapping. Nothing fancy yet -- config/remapping is
// a later problem, same spirit as "sound is a fancy problem, tackle last".
static u16 KeycodeToGbaBit(SDL_Keycode key)
{
    switch (key)
    {
        case SDLK_z:         return A_BUTTON;
        case SDLK_x:         return B_BUTTON;
        case SDLK_BACKSPACE: return SELECT_BUTTON;
        case SDLK_RETURN:    return START_BUTTON;
        case SDLK_RIGHT:     return DPAD_RIGHT;
        case SDLK_LEFT:      return DPAD_LEFT;
        case SDLK_UP:        return DPAD_UP;
        case SDLK_DOWN:      return DPAD_DOWN;
        case SDLK_s:         return R_BUTTON;
        case SDLK_a:         return L_BUTTON;
        default:             return 0;
    }
}

static void Host_InitFrontend(void)
{
    SDL_Init(SDL_INIT_VIDEO);

    sWindow = SDL_CreateWindow(
        "RetroForge - Pokemon FireRed",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        DISPLAY_WIDTH * HOST_SCALE, DISPLAY_HEIGHT * HOST_SCALE,
        0
    );
    sRenderer = SDL_CreateRenderer(sWindow, -1, SDL_RENDERER_ACCELERATED);
    sTexture = SDL_CreateTexture(
        sRenderer, SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING, DISPLAY_WIDTH, DISPLAY_HEIGHT
    );

    // Real hardware idles with no keys pressed = all bits 1 (active-low,
    // pull-up resistors). ReadKeys() in main.c does
    // `REG_KEYINPUT ^ KEYS_MASK` to flip this into "1 = pressed" logic.
    REG_KEYINPUT = KEYS_MASK;
}

static void PumpEventsAndUpdateKeys(void)
{
    SDL_Event e;
    while (SDL_PollEvent(&e))
    {
        if (e.type == SDL_QUIT)
        {
            SDL_DestroyTexture(sTexture);
            SDL_DestroyRenderer(sRenderer);
            SDL_DestroyWindow(sWindow);
            SDL_Quit();
            exit(0);
        }
        if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP)
        {
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_p)
                sScreenshotRequested = 1;

            u16 bit = KeycodeToGbaBit(e.key.keysym.sym);
            if (bit)
            {
                if (e.type == SDL_KEYDOWN)
                    REG_KEYINPUT &= ~bit;  // pressed = 0 (active-low)
                else
                    REG_KEYINPUT |= bit;   // released = 1
            }
        }
    }
}

// OBJ shape/size -> pixel dimensions. Indexed [shape][size], both taken
// straight from ATTR0 bits 14-15 (shape) and ATTR1 bits 14-15 (size).
// This is the real GBA table -- shape 3 is undefined hardware behavior,
// left as 8x8 here just so nothing reads out of bounds.
static const struct { u8 w, h; } sObjSizeTable[4][4] =
{
    /* shape 0: square     */ {{8,8},   {16,16}, {32,32}, {64,64}},
    /* shape 1: horizontal */ {{16,8},  {32,8},  {32,16}, {64,32}},
    /* shape 2: vertical   */ {{8,16},  {8,32},  {16,32}, {32,64}},
    /* shape 3: invalid    */ {{8,8},   {8,8},   {8,8},   {8,8}}
};

// Draws one regular (text-mode, non-affine) background layer into the
// framebuffer, honoring its own screen-size (32x32 / 64x32 / 32x64 /
// 64x64 tile map), 4bpp vs 8bpp tile depth, and hardware scroll
// (HOFS/VOFS) registers. This does NOT handle affine BGs (rotation/
// scaling) -- if the star-field intro turns out to use an affine BG2/3,
// this still won't draw it; that needs BGxPA/PB/PC/PD matrix math on
// top of this, which is a separate, larger piece of work.
static void DrawBgLayer(void *pixels, int pitch, u16 bgcnt, u16 hofs, u16 vofs)
{
    u16 *pltt = (u16 *)gGbaPltt;
    u32 charBase = ((bgcnt >> 2) & 3) * 0x4000;
    u32 mapBase  = ((bgcnt >> 8) & 31) * 0x800;
    u8  screenSizeBits = (bgcnt >> 14) & 3;
    int is8bpp = (bgcnt & (1 << 7)) != 0;

    // Tile-map width/height in tiles for each of the 4 possible layouts.
    // Layout of 64-wide/64-tall maps is 2 (or 4) 32x32 "screen blocks"
    // laid out left-to-right then top-to-bottom in VRAM, 0x800 apart.
    int mapTilesW = (screenSizeBits == 1 || screenSizeBits == 3) ? 64 : 32;
    int mapTilesH = (screenSizeBits == 2 || screenSizeBits == 3) ? 64 : 32;

    for (int screenY = 0; screenY < DISPLAY_HEIGHT; screenY++)
    {
        int bgY = (screenY + vofs) % (mapTilesH * 8);
        int ty  = bgY / 8;
        int py  = bgY % 8;

        for (int screenX = 0; screenX < DISPLAY_WIDTH; screenX++)
        {
            int bgX = (screenX + hofs) % (mapTilesW * 8);
            int tx  = bgX / 8;
            int px  = bgX % 8;

            // Which 32x32 screen-block this tile falls in, and the
            // block's base offset in VRAM.
            int blockX = tx / 32;
            int blockY = ty / 32;
            int blocksPerRow = mapTilesW / 32;
            int blockIndex = blockY * blocksPerRow + blockX;
            u32 blockBase = mapBase + (u32)blockIndex * 0x800;

            int localTx = tx % 32;
            int localTy = ty % 32;
            u16 mapEntry = *(u16 *)&gGbaVram[blockBase + (localTy * 32 + localTx) * 2];
            u16 tileId = mapEntry & 0x3FF;
            int flipX = (mapEntry & 0x400) != 0;
            int flipY = (mapEntry & 0x800) != 0;
            u16 paletteBank = (mapEntry >> 12) & 0xF;

            int sampleX = flipX ? (7 - px) : px;
            int sampleY = flipY ? (7 - py) : py;

            u16 color;
            if (!is8bpp)
            {
                u8 byte = gGbaVram[charBase + tileId * 32 + sampleY * 4 + sampleX / 2];
                u8 colorIdx = (sampleX % 2 == 0) ? (byte & 0x0F) : (byte >> 4);
                if (colorIdx == 0)
                    continue; // transparent, let a lower-priority layer show through
                color = pltt[paletteBank * 16 + colorIdx];
            }
            else
            {
                u8 colorIdx = gGbaVram[charBase + tileId * 64 + sampleY * 8 + sampleX];
                if (colorIdx == 0)
                    continue;
                color = pltt[colorIdx]; // 8bpp tiles always use the full 256-color palette
            }

            u8 r = (color & 0x1F) << 3;
            u8 g = ((color >> 5) & 0x1F) << 3;
            u8 b = ((color >> 10) & 0x1F) << 3;
            u32 rgba = ((u32)r << 24) | ((u32)g << 16) | ((u32)b << 8) | 0xFF;

            u32 *row = (u32 *)((u8 *)pixels + screenY * pitch);
            row[screenX] = rgba;
        }
    }
}

// PLACEHOLDER, upgraded from "BG0 + fixed 16x16 sprites only" to a
// multi-layer compositor: draws BG0-BG3 in real hardware priority order
// (bits 0-1 of each BGxCNT; lower value = drawn on top) with scroll
// support, then sprites on top sized from their real OBJ shape/size
// bits instead of being hardcoded to 16x16. Still NOT implemented:
// affine backgrounds/sprites (rotation & scaling -- BGxPA/PB/PC/PD,
// OBJ attr0 bit 8), mosaic, and the BLDCNT/BLDY alpha/brightness blend
// used for fades. If a screen still shows wrong/black after this, the
// next suspect is one of those, most likely a brightness fade (BLDY)
// leaving the framebuffer at "faded" values we never blend back in.
static void RenderPlaceholderFrame(void)
{
    u16 *pltt = (u16 *)gGbaPltt;
    void *pixels;
    int pitch;
    
    SDL_LockTexture(sTexture, NULL, &pixels, &pitch);

    // 1. Draw the backdrop (Solid background color from Palette 0)
    u16 bgr_backdrop = pltt[0];
    u32 rgba_backdrop = (((bgr_backdrop & 0x1F) << 3) << 24) | 
                        ((((bgr_backdrop >> 5) & 0x1F) << 3) << 16) | 
                        ((((bgr_backdrop >> 10) & 0x1F) << 3) << 8) | 0xFF;

    for (int y = 0; y < DISPLAY_HEIGHT; y++)
    {
        u32 *row = (u32 *)((u8 *)pixels + y * pitch);
        for (int x = 0; x < DISPLAY_WIDTH; x++)
            row[x] = rgba_backdrop;
    }

    // 2. Draw all four regular BG layers, lowest hardware priority first
    //    (priority 3) up to highest (priority 0), so priority-0 content
    //    ends up on top -- matches real GBA layering rules. BG2/BG3 are
    //    only correct here if the game is actually running them in text
    //    mode; if DISPCNT's mode bits say mode 1 or 2, BG2/3 are affine
    //    on real hardware and this text-mode path will misread them.
    u8 dispcntMode = REG_DISPCNT & 0x7;
    struct { u16 cnt; u16 hofs, vofs; u32 onFlag; } bgs[4] = {
        { REG_BG0CNT, REG_BG0HOFS, REG_BG0VOFS, DISPCNT_BG0_ON },
        { REG_BG1CNT, REG_BG1HOFS, REG_BG1VOFS, DISPCNT_BG1_ON },
        { REG_BG2CNT, REG_BG2HOFS, REG_BG2VOFS, DISPCNT_BG2_ON },
        { REG_BG3CNT, REG_BG3HOFS, REG_BG3VOFS, DISPCNT_BG3_ON },
    };

    for (int prio = 3; prio >= 0; prio--)
    {
        for (int bg = 0; bg < 4; bg++)
        {
            // BG2/BG3 in affine modes (1, 2) need matrix-based rendering
            // we don't do yet -- skip rather than misread them as text BGs.
            int isAffineOnly = (bg == 3 && dispcntMode == 1) ||
                                 ((bg == 2 || bg == 3) && dispcntMode == 2);
            if (isAffineOnly)
                continue;

            if (!(REG_DISPCNT & bgs[bg].onFlag))
                continue;
            if ((bgs[bg].cnt & 3) != prio)
                continue;

            DrawBgLayer(pixels, pitch, bgs[bg].cnt, bgs[bg].hofs, bgs[bg].vofs);
        }
    }

    // 3. Draw Sprites (OAM), sized from their real shape/size bits instead
    // of a hardcoded 16x16. Sprite-vs-BG priority interleaving (a sprite
    // with priority 2 should sit behind a priority-1 BG) is NOT modeled --
    // all sprites are drawn on top of every BG layer here, which is a
    // simplification, not real hardware behavior.
    //
    // Gated on DISPCNT bit 12 (global OBJ enable) -- without this, once a
    // screen turns sprites off, whatever was last sitting in OAM (e.g. a
    // previous screen's sprites, never cleared) gets drawn anyway. That's
    // exactly what the "floating blobs on a flat backdrop" screenshot was:
    // real leftover star-sprite data from the previous screen, rendered
    // during a screen where the game never intended sprites to show at all.
    int spritesDrawnCount = 0;
    int affineSpriteCount = 0;
    if (REG_DISPCNT & (1 << 12))
    {
        for (int i = 0; i < 128; i++)
        {
            u16 attr0 = *(u16*)&gGbaOam[i * 8 + 0];
            u16 attr1 = *(u16*)&gGbaOam[i * 8 + 2];
            u16 attr2 = *(u16*)&gGbaOam[i * 8 + 4];

            int isAffine = (attr0 & (1 << 8)) != 0;

            // Non-affine sprites: bit 9 is the "disabled" flag.
            // Affine sprites: bit 9 is "double-size" instead, and there is
            // no disable flag -- an affine OBJ is only hidden by having its
            // affine index or size make it draw nothing.
            if (!isAffine && (attr0 & 0x200)) continue;

            if (isAffine) affineSpriteCount++;

            // NOT YET HANDLED: affine sprites need their pixel positions
            // run through a rotation/scaling matrix (stored elsewhere in
            // OAM, indexed by attr1 bits 9-13) instead of being sampled
            // straight through. Drawing them here without that transform
            // means an affine sprite's shape/position on screen will be
            // wrong -- possibly wrong enough to look absent. If the trace
            // below shows affine sprites present exactly when stars should
            // be visible, that's the next thing to actually implement.
            if (isAffine) continue;

            spritesDrawnCount++;

            int y = attr0 & 0xFF;
            int x = attr1 & 0x1FF;
            if (x >= 240) x -= 512;
            if (y >= 160) y -= 256;

            u8 shape = (attr0 >> 14) & 3;
            u8 size  = (attr1 >> 14) & 3;
            int objW = sObjSizeTable[shape][size].w;
            int objH = sObjSizeTable[shape][size].h;
            int tilesPerRow = objW / 8;

            u16 tileId = attr2 & 0x3FF;
            u16 paletteBank = (attr2 >> 12) & 0xF;
            int is8bpp = (attr0 & (1 << 13)) != 0;
            int flipX = (attr1 & (1 << 12)) != 0;
            int flipY = (attr1 & (1 << 13)) != 0;

            for (int py = 0; py < objH; py++)
            {
                for (int px = 0; px < objW; px++)
                {
                    int screenX = x + px;
                    int screenY = y + py;
                    if (screenX < 0 || screenX >= DISPLAY_WIDTH || screenY < 0 || screenY >= DISPLAY_HEIGHT) continue;

                    int sx = flipX ? (objW - 1 - px) : px;
                    int sy = flipY ? (objH - 1 - py) : py;
                    int tileX = sx / 8, tileY = sy / 8;
                    int localPx = sx % 8, localPy = sy % 8;

                    // In 1D OBJ mapping (the common case for these decomps),
                    // tiles run left-to-right then wrap to the next row.
                    int currentTileId = tileId + (is8bpp ? (tileY * tilesPerRow + tileX) * 2
                                                          : tileY * tilesPerRow + tileX);
                    u32 objBase = 0x10000;

                    u16 color;
                    if (!is8bpp)
                    {
                        u8 pixelByte = gGbaVram[objBase + (currentTileId * 32) + (localPy * 4) + (localPx / 2)];
                        u8 colorIdx = (localPx % 2 == 0) ? (pixelByte & 0x0F) : (pixelByte >> 4);
                        if (colorIdx == 0) continue;
                        color = pltt[256 + paletteBank * 16 + colorIdx];
                    }
                    else
                    {
                        u8 colorIdx = gGbaVram[objBase + (currentTileId * 32) + localPy * 8 + localPx];
                        if (colorIdx == 0) continue;
                        color = pltt[256 + colorIdx];
                    }

                    u32 rgba = (((color & 0x1F) << 3) << 24) | ((((color >> 5) & 0x1F) << 3) << 16) | ((((color >> 10) & 0x1F) << 3) << 8) | 0xFF;
                    u32 *row = (u32 *)((u8 *)pixels + screenY * pitch);
                    row[screenX] = rgba;
                }
            }
        }
    }

    SDL_UnlockTexture(sTexture);
    SDL_RenderClear(sRenderer);
    SDL_RenderCopy(sRenderer, sTexture, NULL, NULL);
    SDL_RenderPresent(sRenderer);

    static u32 sFrameCount = 0;
    sFrameCount++;

    // Press P at any point to dump the exact current frame (upscaled, as
    // actually drawn) plus a text file with every register value used to
    // draw it -- both timestamped together so they're guaranteed to match.
    // This replaces guessing from periodic register dumps: when a screen
    // looks wrong, capture it, and the .txt tells us exactly what BG/OBJ
    // state produced that specific picture.
    if (sScreenshotRequested)
    {
        sScreenshotRequested = 0;

        int winW, winH;
        SDL_GetWindowSize(sWindow, &winW, &winH);
        SDL_Surface *shot = SDL_CreateRGBSurfaceWithFormat(0, winW, winH, 32, SDL_PIXELFORMAT_RGBA32);
        if (shot)
        {
            if (SDL_RenderReadPixels(sRenderer, NULL, SDL_PIXELFORMAT_RGBA32, shot->pixels, shot->pitch) == 0)
            {
                char bmpPath[64], txtPath[64];
                snprintf(bmpPath, sizeof(bmpPath), "debug_frame_%u.bmp", sFrameCount);
                snprintf(txtPath, sizeof(txtPath), "debug_frame_%u.txt", sFrameCount);
                SDL_SaveBMP(shot, bmpPath);

                FILE *f = fopen(txtPath, "w");
                if (f)
                {
                    fprintf(f,
                        "frame=%u DISPCNT=0x%04X mode=%d BG0/1/2/3 on=%d/%d/%d/%d "
                        "cnt=0x%04X/0x%04X/0x%04X/0x%04X OBJ_on=%d drawn=%d affine_skipped=%d\n",
                        sFrameCount, REG_DISPCNT, dispcntMode,
                        (REG_DISPCNT & DISPCNT_BG0_ON) != 0, (REG_DISPCNT & DISPCNT_BG1_ON) != 0,
                        (REG_DISPCNT & DISPCNT_BG2_ON) != 0, (REG_DISPCNT & DISPCNT_BG3_ON) != 0,
                        bgs[0].cnt, bgs[1].cnt, bgs[2].cnt, bgs[3].cnt,
                        (REG_DISPCNT & (1 << 12)) != 0, spritesDrawnCount, affineSpriteCount);
                    fclose(f);
                }
                fprintf(stderr, "[screenshot] saved %s + %s\n", bmpPath, txtPath);
            }
            SDL_FreeSurface(shot);
        }
    }

    // Lightweight diagnostic trace -- cheap enough to leave on while you're
    // chasing the "goes black" transitions. If the frame counter keeps
    // climbing while the screen looks stuck black, it's a missing-layer/
    // blend problem (see the caveats above this function), NOT a hang.
    // If the counter stops incrementing and the window ignores clicks,
    // that's a real hang, same class as the earlier CheckForFlashMemory/
    // InitRFU spin -- go back to the fprintf/gdb approach from step 5 of
    // the handoff doc, but starting from wherever this trace last fired.
    if (sFrameCount % 120 == 0)
    {
        fprintf(stderr,
            "[frame %u] DISPCNT=0x%04X mode=%d BG0/1/2/3 on=%d/%d/%d/%d cnt=0x%04X/0x%04X/0x%04X/0x%04X "
            "OBJ_on=%d drawn=%d affine_skipped=%d\n",
            sFrameCount, REG_DISPCNT, dispcntMode,
            (REG_DISPCNT & DISPCNT_BG0_ON) != 0, (REG_DISPCNT & DISPCNT_BG1_ON) != 0,
            (REG_DISPCNT & DISPCNT_BG2_ON) != 0, (REG_DISPCNT & DISPCNT_BG3_ON) != 0,
            bgs[0].cnt, bgs[1].cnt, bgs[2].cnt, bgs[3].cnt,
            (REG_DISPCNT & (1 << 12)) != 0, spritesDrawnCount, affineSpriteCount);
    }
}

// Called from main.c's WaitForVBlank() once per real GBA frame, and also
// directly from VBlankIntrWait() in agbmain_syscalls_host.c. This is the
// single point where "host" and "hardware" meet each frame:
//   1. pump SDL input into REG_KEYINPUT (so ReadKeys() sees it next loop)
//   2. run the REAL VBlankIntr() via gIntrTable[4] -- sound mix, gpu reg
//      flush, link/RNG upkeep, all genuinely unmodified game code
//   3. draw whatever we can (currently: backdrop color only)
//   4. pace to real GBA frame timing (59.7275 Hz)
void Host_RunFrame(void)
{
    static Uint32 sLastTicks = 0;
    const double frameMs = 1000.0 / GBA_REFRESH_HZ;

    PumpEventsAndUpdateKeys();

    // index 4 == VBlankIntr in gIntrTableTemplate's fixed ordering
    // (VCount, Serial, Timer3, HBlank, VBlank, ...) -- see main.c.
    // VBlankIntr() itself sets INTR_CHECK / gMain.intrCheck at its end,
    // so nothing further is needed here.
    gIntrTable[4]();

    RenderPlaceholderFrame();

    Uint32 now = SDL_GetTicks();
    Uint32 elapsed = now - sLastTicks;
    if (elapsed < frameMs)
        SDL_Delay((Uint32)(frameMs - elapsed));
    sLastTicks = SDL_GetTicks();
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    InitGbaMemoryHost();
    Host_InitFrontend();

    // SoftReset() longjmps back here (see agbmain_syscalls_host.c) --
    // this re-enters AgbMain() from the top, same effective behavior as
    // real hardware jumping back to the ROM entry point.
    setjmp(gAgbMainResetPoint);

    AgbMain();  // never returns in practice, matches real hardware

    return 0;
}

#endif // HOST_BUILD
