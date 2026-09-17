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
    u16 bgr555 = pltt[0];

    u8 r = (bgr555 & 0x1F) << 3;
    u8 g = ((bgr555 >> 5) & 0x1F) << 3;
    u8 b = ((bgr555 >> 10) & 0x1F) << 3;

    void *pixels;
    int pitch;
    SDL_LockTexture(sTexture, NULL, &pixels, &pitch);
    for (int y = 0; y < DISPLAY_HEIGHT; y++)
    {
        u32 *row = (u32 *)((u8 *)pixels + y * pitch);
        for (int x = 0; x < DISPLAY_WIDTH; x++)
            row[x] = ((u32)r << 24) | ((u32)g << 16) | ((u32)b << 8) | 0xFF;
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
