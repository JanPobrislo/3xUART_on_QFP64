/******************************************************************************
 * Obsluha TIMER0 - preruseni 1 Hz pro mereni casu ve vterinach.
 *****************************************************************************/

#ifndef TIMER0_H
#define TIMER0_H

//#include "em_system.h"
#include "em_cmu.h"

#define TIMER0_DIVIDER   8
#define TIMER0_TOP       (72000000UL / 1024 / TIMER0_DIVIDER - 1)  /* 8789 */
static volatile uint8_t timer0_count = 0;


extern unsigned char IsSecond;		//-- Flag nahozeny v interaptu
extern unsigned long SecondCounter; //-- Pocitadlo vterin

void initTIMER0(void);
void TIMER0_Start(void);
void TIMER0_Stop(void);

#endif /* TIMER0_H */
