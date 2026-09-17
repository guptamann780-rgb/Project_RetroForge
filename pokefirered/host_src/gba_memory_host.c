#include "gba/types.h"
#include "gba_memory_host.h"
#include <string.h>

// Real hardware backing for these is physical silicon that either
// powers up in a defined state or doesn't matter until the game
// explicitly sets it (DISPCNT etc. are always written by AgbMain /
// InitGpuRegManager before anything reads them). Zero-init is a safe,
// deterministic starting point and matches what RegisterRamReset(RESET_ALL)
// would leave behind anyway.

u8 gGbaIoRegs[GBA_IOREG_SIZE];
u8 gGbaPltt[GBA_PLTT_SIZE];
u8 gGbaVram[GBA_VRAM_SIZE];
u8 gGbaOam[GBA_OAM_SIZE];

struct SoundInfo *gGbaSoundInfoPtr;
u16 gGbaIntrCheck;
void *gGbaIntrVector;
jmp_buf gAgbMainResetPoint;

void InitGbaMemoryHost(void)
{
    memset(gGbaIoRegs, 0, GBA_IOREG_SIZE);
    memset(gGbaPltt, 0, GBA_PLTT_SIZE);
    memset(gGbaVram, 0, GBA_VRAM_SIZE);
    memset(gGbaOam, 0, GBA_OAM_SIZE);

    gGbaSoundInfoPtr = NULL;
    gGbaIntrCheck = 0;
    gGbaIntrVector = NULL;
}
