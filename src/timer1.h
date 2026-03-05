/******************************************************************************
 * Obsluha TIMER1 - preruseni 1200 Hz pro TX POCSAG
 *****************************************************************************/

#ifndef TIMER1_H
#define TIMER1_H

void initTIMER1(void);
void TIMER1_Start(void);
void TIMER1_Stop(void);

#endif /* TIMER1_H */