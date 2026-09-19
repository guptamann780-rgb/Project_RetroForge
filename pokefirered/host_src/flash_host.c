// Host replacement for src/agb_flash.c: 128KB flash emulated in RAM,
// persisted to pokefirered.sav. Real ReadFlash copies ARM code into RAM and
// executes it, which cannot work on x86, so the whole driver is replaced.
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define FLASH_SIZE   0x20000
#define SECTOR_BYTES 0x1000
#define NUM_SECTORS  (FLASH_SIZE / SECTOR_BYTES)
#define SAVE_PATH    "pokefirered.sav"

static uint8_t sFlash[FLASH_SIZE];
static int sInit = 0;

static void FlashInit(void)
{
    FILE *f;
    if (sInit) return;
    sInit = 1;
    memset(sFlash, 0xFF, sizeof(sFlash));
    f = fopen(SAVE_PATH, "rb");
    if (f) {
        size_t n = fread(sFlash, 1, sizeof(sFlash), f);
        (void)n;
        fclose(f);
    }
}

static void FlashPersist(void)
{
    FILE *f = fopen(SAVE_PATH, "wb");
    if (f) {
        fwrite(sFlash, 1, sizeof(sFlash), f);
        fclose(f);
    }
}

static uint16_t HostEraseFlashSector(uint16_t sectorNum)
{
    FlashInit();
    if (sectorNum >= NUM_SECTORS) return 1;
    memset(&sFlash[sectorNum * SECTOR_BYTES], 0xFF, SECTOR_BYTES);
    FlashPersist();
    return 0;
}

static uint16_t HostEraseFlashChip(void)
{
    FlashInit();
    memset(sFlash, 0xFF, sizeof(sFlash));
    FlashPersist();
    return 0;
}

static uint16_t HostProgramFlashByte(uint16_t sectorNum, uint32_t offset, uint8_t data)
{
    FlashInit();
    if (sectorNum >= NUM_SECTORS || offset >= SECTOR_BYTES) return 1;
    sFlash[sectorNum * SECTOR_BYTES + offset] = data;
    return 0;
}

static uint16_t HostProgramFlashSector(uint16_t sectorNum, void *src)
{
    FlashInit();
    if (sectorNum >= NUM_SECTORS) return 1;
    memcpy(&sFlash[sectorNum * SECTOR_BYTES], src, SECTOR_BYTES);
    FlashPersist();
    return 0;
}

// ---- globals normally defined in agb_flash.c ----
uint8_t gFlashTimeoutFlag = 0;
uint8_t (*PollFlashStatus)(uint8_t *) = NULL;
uint16_t (*WaitForFlashWrite)(uint8_t, uint8_t *, uint8_t) = NULL;
uint16_t (*ProgramFlashSector)(uint16_t, void *) = HostProgramFlashSector;
const void *gFlash = NULL;
uint16_t (*ProgramFlashByte)(uint16_t, uint32_t, uint8_t) = HostProgramFlashByte;
uint16_t gFlashNumRemainingBytes = 0;
uint16_t (*EraseFlashChip)(void) = HostEraseFlashChip;
uint16_t (*EraseFlashSector)(uint16_t) = HostEraseFlashSector;
const uint16_t *gFlashMaxTime = NULL;

// ---- functions normally defined in agb_flash.c ----
void SwitchFlashBank(uint8_t bankNum) { (void)bankNum; }
uint16_t ReadFlashId(void) { return 0x1CC2; }
void FlashTimerIntr(void) {}
uint16_t SetFlashTimerIntr(uint8_t timerNum, void (**intrFunc)(void)) { (void)timerNum; (void)intrFunc; return 0; }
void StartFlashTimer(uint8_t phase) { (void)phase; }
void StopFlashTimer(void) {}
uint8_t ReadFlash1(uint8_t *addr) { return *addr; }
void SetReadFlash1(uint16_t *dest) { (void)dest; }
void ReadFlash_Core(volatile uint8_t *src, uint8_t *dest, uint32_t size)
{
    uint32_t i;
    for (i = 0; i < size; i++) dest[i] = src[i];
}

void ReadFlash(uint16_t sectorNum, uint32_t offset, void *dest, uint32_t size)
{
    FlashInit();
    if (sectorNum >= NUM_SECTORS || offset + size > SECTOR_BYTES) return;
    memcpy(dest, &sFlash[sectorNum * SECTOR_BYTES + offset], size);
}

uint32_t VerifyFlashSector_Core(uint8_t *src, uint8_t *tgt, uint32_t size)
{
    uint32_t i;
    for (i = 0; i < size; i++)
        if (src[i] != tgt[i]) return i + 1;
    return 0;
}

uint32_t VerifyFlashSector(uint16_t sectorNum, uint8_t *src)
{
    FlashInit();
    if (sectorNum >= NUM_SECTORS) return 1;
    return VerifyFlashSector_Core(src, &sFlash[sectorNum * SECTOR_BYTES], SECTOR_BYTES);
}

uint32_t VerifyFlashSectorNBytes(uint16_t sectorNum, uint8_t *src, uint32_t n)
{
    FlashInit();
    if (sectorNum >= NUM_SECTORS) return 1;
    return VerifyFlashSector_Core(src, &sFlash[sectorNum * SECTOR_BYTES], n);
}

uint32_t ProgramFlashSectorAndVerify(uint16_t sectorNum, uint8_t *src)
{
    if (HostEraseFlashSector(sectorNum) != 0) return 1;
    return HostProgramFlashSector(sectorNum, src);
}

uint32_t ProgramFlashSectorAndVerifyNBytes(uint16_t sectorNum, void *dataSrc, uint32_t n)
{
    FlashInit();
    if (sectorNum >= NUM_SECTORS || n > SECTOR_BYTES) return 1;
    if (HostEraseFlashSector(sectorNum) != 0) return 1;
    memcpy(&sFlash[sectorNum * SECTOR_BYTES], dataSrc, n);
    FlashPersist();
    return 0;
}
