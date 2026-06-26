#include <stdint.h>

#define REG_SET_BIT(ADDR,BIT) ((*(volatile uint32_t*)(ADDR)) |= (((uint32_t)1) << (BIT)))

#define REG_CLEAR_BIT(ADDR,BIT) ((*(volatile uint32_t*)(ADDR)) &= ~(((uint32_t)1) << (BIT)))

#define REG_WRITE_FIELD(ADDR,SHIFT,WIDTH,VAL) ((*(volatile uint32_t*)(ADDR))= (((*(volatile uint32_t*)(ADDR))& (~(((((uint32_t)1)<<(WIDTH))-((uint32_t)1))<<(SHIFT)))) | (((uint32_t)(VAL) & ((((uint32_t)1)<<(WIDTH))-((uint32_t)1)))<< SHIFT)))

#define REG_READ_FIELD(ADDR,SHIFT,WIDTH) (((*(volatile uint32_t*)(ADDR)) & (((((uint32_t)1) << (WIDTH)) -1)<<(SHIFT))) >> (SHIFT))