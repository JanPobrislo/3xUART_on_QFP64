/******************************************************************************
 * Obsluha TIMER1 - preruseni 1200 Hz pro TX POCSAG
 *****************************************************************************/

#ifndef TIMER1_H
#define TIMER1_H

/* 2400 Hz: 72 000 000 / 16 / 2400 - 1 = 1874 */
/* 1200 Hz: 3749 pøi 72MHz a div16 */
#define TIMER1_TOP  (72000000UL / 16 / 1200 - 1)

void initTIMER1(void);
void TIMER1_Start(void);
void TIMER1_Stop(void);

#endif /* TIMER1_H */
