/******************************************************************************
 * @file timer0.c
 * @brief Obsluha TIMER0 - preruseni 1 Hz pro toggle LED2
 * @note HFCLK = 72 MHz, PRESC = DIV1024, TOP = 8789, softwarovy delitel = 8
 *       72 000 000 / 1024 / 8790 / 8 = 1,0005 Hz
 *****************************************************************************/
#include "timer0.h"
#include "led.h"
#include "em_cmu.h"
#include "em_timer.h"

unsigned char IsSecond;		 //-- Flag nahozeny v interaptu
unsigned long SecondCounter; //-- Pocitadlo vterin

void initTIMER0(void)
{
    CMU_ClockEnable(cmuClock_TIMER0, true);

    TIMER0->CTRL = 0;
    TIMER0->CNT  = 0;
    TIMER0->TOP  = TIMER0_TOP;
    TIMER0->CTRL = TIMER_CTRL_PRESC_DIV1024 | TIMER_CTRL_MODE_UP;
    TIMER0->IFC  = _TIMER_IFC_MASK;
    TIMER0->IEN  = TIMER_IEN_OF;

    NVIC_ClearPendingIRQ(TIMER0_IRQn);
    NVIC_EnableIRQ(TIMER0_IRQn);

    TIMER0->CMD = TIMER_CMD_START;

    IsSecond = 0;
    SecondCounter = 0;
}

void TIMER0_Start(void)
{
    timer0_count = 0;    //----- zajisti reset citace (jinak by pokracoval od posledni hodnoty)
    TIMER0->CNT  = 0;
    TIMER0->IFC  = _TIMER_IFC_MASK;
    NVIC_ClearPendingIRQ(TIMER0_IRQn);
    NVIC_EnableIRQ(TIMER0_IRQn);
    TIMER0->CMD  = TIMER_CMD_START;
}

void TIMER0_Stop(void)
{
    TIMER0->CMD = TIMER_CMD_STOP;
    NVIC_DisableIRQ(TIMER0_IRQn);
}

void TIMER0_IRQHandler(void)
{
    TIMER0->IFC = TIMER_IFC_OF;
    if (++timer0_count >= TIMER0_DIVIDER) {
        timer0_count = 0;
//        LED2_Toggle();   // Toggle pøesnì 1x za sekundu
        IsSecond = 1;
        SecondCounter++;
    }
}
