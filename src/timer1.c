/******************************************************************************
 * @file timer1.c
 * @brief Obsluha TIMER1 - preruseni 1200 Hz pro toggle vystupu TX (PA1)
 * @note HFCLK = 72 MHz, PRESC = DIV16, TOP = 3749
 *       72 000 000 / 16 / 3750 = 1200 Hz
 *****************************************************************************/
#include "timer1.h"
#include "ports.h"
#include "pocsag.h"
#include "led.h"

#include "em_cmu.h"
#include "em_timer.h"
#include "em_gpio.h"

//---- 1200 Hz
//#define TIMER1_TOP  (72000000UL / 16 / 1200 - 1)  /* 3749 */
//---- 2400 Hz
#define TIMER1_TOP  (72000000UL / 16 / 2400 - 1)  /* 1874 - dvojnasobna rychlost */

void initTIMER1(void)
{
    CMU_ClockEnable(cmuClock_TIMER1, true);

    TIMER1->CTRL = 0;
    TIMER1->CNT  = 0;
    TIMER1->TOP  = TIMER1_TOP;
    TIMER1->CTRL = TIMER_CTRL_PRESC_DIV16 | TIMER_CTRL_MODE_UP;
    TIMER1->IFC  = _TIMER_IFC_MASK;
    TIMER1->IEN  = TIMER_IEN_OF;

    NVIC_ClearPendingIRQ(TIMER1_IRQn);
    NVIC_EnableIRQ(TIMER1_IRQn);

    TIMER1->CMD = TIMER_CMD_START;
}

void TIMER1_Start(void)
{
    TIMER1->CNT  = 0;     //----- zajisti reset citace (jinak by pokracoval od posledni hodnoty)
    TIMER1->IFC  = _TIMER_IFC_MASK;
    NVIC_ClearPendingIRQ(TIMER1_IRQn);
    NVIC_EnableIRQ(TIMER1_IRQn);
    TIMER1->CMD  = TIMER_CMD_START;
}

void TIMER1_Stop(void)
{
    TIMER1->CMD = TIMER_CMD_STOP;
    NVIC_DisableIRQ(TIMER1_IRQn);
}

void TIMER1_IRQHandler(void)
{
    TIMER1->IFC = TIMER_IFC_OF;
    POCSAG_SampleBit();          /* vzorkovani POCSAG bitu z PA0 */
    GPIO_PinOutToggle(TX_PORT, TX_PIN);
    LED_TX_Toggle();
}
