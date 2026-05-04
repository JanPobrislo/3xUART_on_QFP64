/******************************************************************************
 * WTIMER0 (32 bitovy) bìží na maximální možné frekvenci 72 MHz, aby mohl
 * zmìøit délku trvání nìkolika bitu preamble s rozlišením 13,8 nanosekundy.
 * Z namìøené doby skuteèné rychlosti pak pøepoèítáme TOP hodnotu pro TIMER1.
 *
 * 1200b/s je vzorkovano 72Mhz => jeden bit odpovida 60000 tikum èítaèe.
 *****************************************************************************/
#ifndef WTIMER0_H
#define WTIMER0_H

#include <stdint.h>
#include <stdbool.h>

extern volatile uint32_t last_edge_wtimer;
extern volatile uint32_t measured_diff_wtimer;
extern volatile bool new_edge_data;

void initWTIMER0(void);

#endif
