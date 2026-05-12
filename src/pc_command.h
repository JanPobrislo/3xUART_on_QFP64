//------------------------------------------------------------------------------
//  Komunikace s PC na COM-A (UART0,9600)
//  Spolupracuje s programy: Master + TCImulti (nastaveni parametru)
//------------------------------------------------------------------------------
#include "pocsag.h"

extern POCSAG_token master_token; // token prijaty z PC

void read_pc_byte(unsigned char zn);
