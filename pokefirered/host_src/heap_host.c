#include "gba/types.h"
#include "malloc.h"
        // pulls in HEAP_SIZE's real definition

// Real GBA build: gHeap is just a linker-script marker (gHeap = .; in ld_script.ld) pointing into EWRAM — not a real sized array, the 
// linker just reserves space after it. Host build has no such linker-script trick available, so it needs a real, concretely-sized array 
// instead.

u8 gHeap[HEAP_SIZE];
