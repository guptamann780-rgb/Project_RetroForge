#ifdef HOST_BUILD

#include <SDL2/SDL.h>
#include <setjmp.h>
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

// PLACEHOLDER. The real compositor (reads VRAM/OAM/PLTT, draws tiles +
// sprites per BG/OBJ registers) is explicitly "not started yet" per your
// own project notes -- that's real, substantial work, not something to
// fake here. This just proves palette RAM is alive end-to-end: it reads
// the actual backdrop color the game is really writing via
// SetBackdropFromColor()/palette fades, converts real GBA BGR555 to
// RGBA8888, and fills the screen with it. One correct pixel, not zero.
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

    // 2. Draw Background 0 (Usually contains menus, text, and logos)
    // Check if BG0 is turned on in the display control register
    if (REG_DISPCNT & DISPCNT_BG0_ON)
    {
        u16 bg0cnt = REG_BG0CNT;
        
        // The GBA uses these bits to know where in VRAM the tile graphics and map layouts live
        u32 charBase = ((bg0cnt >> 2) & 3) * 0x4000;
        u32 mapBase  = ((bg0cnt >> 8) & 31) * 0x800;

        // Loop through the 30x20 grid of tiles that makes up the 240x160 screen
        for (int ty = 0; ty < 20; ty++)
        {
            for (int tx = 0; tx < 30; tx++)
            {
                // Read the tilemap entry (2 bytes per tile)
                u16 mapEntry = *(u16*)&gGbaVram[mapBase + (ty * 32 + tx) * 2];
                u16 tileId = mapEntry & 0x3FF;
                u16 paletteBank = (mapEntry >> 12) & 0xF;

                // Draw the 8x8 pixels for this specific tile
                for (int py = 0; py < 8; py++)
                {
                    for (int px = 0; px < 8; px++)
                    {
                        // 4bpp tile data: 32 bytes per tile, 4 bytes per row.
                        u8 pixelByte = gGbaVram[charBase + (tileId * 32) + (py * 4) + (px / 2)];
                        
                        // Extract the 4-bit color index for this specific pixel
                        u8 colorIdx = (px % 2 == 0) ? (pixelByte & 0x0F) : (pixelByte >> 4);

                        // Color index 0 is transparent, so we only draw if it's > 0
                        if (colorIdx != 0)
                        {
                            u16 color = pltt[paletteBank * 16 + colorIdx];
                            u8 r = (color & 0x1F) << 3;
                            u8 g = ((color >> 5) & 0x1F) << 3;
                            u8 b = ((color >> 10) & 0x1F) << 3;
                            u32 rgba = ((u32)r << 24) | ((u32)g << 16) | ((u32)b << 8) | 0xFF;

                            // Write to SDL texture
                            int screenX = (tx * 8) + px;
                            int screenY = (ty * 8) + py;
                            u32 *row = (u32 *)((u8 *)pixels + screenY * pitch);
                            row[screenX] = rgba;
                        }
                    }
                }
            }
        }
    }

    // 3. Draw Sprites (OAM)
    // The GBA has a maximum of 128 hardware sprites
    for (int i = 0; i < 128; i++)
    {
        // Each sprite is defined by three 16-bit attributes
        u16 attr0 = *(u16*)&gGbaOam[i * 8 + 0];
        u16 attr1 = *(u16*)&gGbaOam[i * 8 + 2];
        u16 attr2 = *(u16*)&gGbaOam[i * 8 + 4];

        // If the disable flag is set, skip drawing this sprite
        if ((attr0 & 0x300) == 0x200) continue; 
        
        // Extract X and Y coordinates on the screen
        int y = attr0 & 0xFF;
        int x = attr1 & 0x1FF;
        if (x >= 240) x -= 512; // Handle off-screen wrapping
        if (y >= 160) y -= 256;

        u16 tileId = attr2 & 0x3FF;
        u16 paletteBank = (attr2 >> 12) & 0xF;
        
        // For right now, we will assume sprites are 16x16 pixels 
        // (A fully complete renderer checks the size/shape bits in ATTR0 and ATTR1)
        for (int py = 0; py < 16; py++)
        {
            for (int px = 0; px < 16; px++)
            {
                int screenX = x + px;
                int screenY = y + py;
                
                // Don't draw pixels that are outside the SDL window
                if (screenX < 0 || screenX >= DISPLAY_WIDTH || screenY < 0 || screenY >= DISPLAY_HEIGHT) continue;
                
                // Calculate which specific 8x8 tile inside the 16x16 sprite we are drawing
                int tileX = px / 8;
                int tileY = py / 8;
                int localPx = px % 8;
                int localPy = py % 8;
                
                // Sprite graphics are stored in the upper half of VRAM (offset 0x10000)
                int currentTileId = tileId + (tileY * 2) + tileX; 
                u32 objBase = 0x10000;
                u8 pixelByte = gGbaVram[objBase + (currentTileId * 32) + (localPy * 4) + (localPx / 2)];
                u8 colorIdx = (localPx % 2 == 0) ? (pixelByte & 0x0F) : (pixelByte >> 4);
                
                // Color index 0 is transparent. If it's not 0, draw it!
                if (colorIdx != 0)
                {
                    // Sprite palettes start exactly halfway through the palette memory (index 256)
                    u16 color = pltt[256 + paletteBank * 16 + colorIdx];
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
