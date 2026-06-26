#include <stdint.h>

//This file is used for manipulating bits of a 32 bit sized variable or register with my own defined MACROS for easy replacment of error prone writing//

//----REG_SET_BIT : Expects address 'ADDR' of register or variable of 32 bits with which 'BIT' to set----//
#define REG_SET_BIT(ADDR,BIT) ((*(volatile uint32_t*)(ADDR)) |= (((uint32_t)1) << (BIT)))

//----REG_CLEAR_BIT : Expects address 'ADDR' of register or variable of 32 bits with which 'BIT' to set----//
#define REG_CLEAR_BIT(ADDR,BIT) ((*(volatile uint32_t*)(ADDR)) &= ~(((uint32_t)1) << (BIT)))

//----REG_WRITE_FIELD : It expexts address for 'ADDR' and uses 'WIDHT' to create 'MASK' to avoid hex calculations----//
//----'SHIFT' used to shift the mask to apply on desired width of bits and use 'VAL' to overwrite the bits without disturbing other values----//
//----BUT PRONE TO DOUBLE EVALUATION IF USED ADDRESS PASSED WITH SIDE EFFECTS LIKE POST INCREMENT----//
#define REG_WRITE_FIELD(ADDR,SHIFT,WIDTH,VAL) ((*(volatile uint32_t*)(ADDR))= (((*(volatile uint32_t*)(ADDR))& (~(((((uint32_t)1)<<(WIDTH))-((uint32_t)1))<<(SHIFT)))) | (((uint32_t)(VAL) & ((((uint32_t)1)<<(WIDTH))-((uint32_t)1)))<< SHIFT)))

//----REG_READ_FIELD : It expexts address for 'ADDR' and uses 'WIDHT' to create 'MASK' to avoid hex calculations----//
//----'SHIFT' used to shift the mask to apply on desired width of bits and produces temporray output of read bits of desired width----//
//----Unlike three others it does not modify the provided address, it's user responsibilty to assign the read value to the appropriate data type----//
#define REG_READ_FIELD(ADDR,SHIFT,WIDTH) (((*(volatile uint32_t*)(ADDR)) & (((((uint32_t)1) << (WIDTH)) -1)<<(SHIFT))) >> (SHIFT))