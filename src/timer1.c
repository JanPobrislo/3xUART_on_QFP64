/******************************************************************************
 * @file timer1.c
 * @brief Obsluha TIMER1 – 1200 Hz pro POCSAG vzorkování
 *
 * @note HFCLK = 72 MHz, PRESC = DIV16, TOP = 1874
 *       72 000 000 / 16 / 1875 = 2400 Hz
 *
 * TIMER1 taktuje POCSAG_SampleBit() 1200x za sekundu.
 * Uvnitø POCSAG_SampleBit() se odeète RX
 * Støed bitu - Synchronizaci fáze zajišuje POCSAG_EdgeDetected() volaná
 * z GPIO_EVEN_IRQHandler pøi hranì signálu na PA0.
 *****************************************************************************/
#include "timer1.h"
#include "ports.h"
#include "pocsag.h"
#include "led.h"

#include "em_cmu.h"
#include "em_timer.h"
#include "em_gpio.h"

/* 2400 Hz: 72 000 000 / 16 / 2400 - 1 = 1874 */
/* 1200 Hz: 3749 pøi 72MHz a div16 */
#define TIMER1_TOP  (72000000UL / 16 / 1200 - 1)

void initTIMER1(void)
{
    CMU_ClockEnable(cmuClock_TIMER1, true);

    TIMER1->CTRL = 0;
    TIMER1->CNT  = 0;
    TIMER1->TOP  = TIMER1_TOP;
    TIMER1->CTRL = TIMER_CTRL_PRESC_DIV16 | TIMER_CTRL_MODE_UP;
    TIMER1->IFC  = _TIMER_IFC_MASK;
    TIMER1->IEN  = TIMER_IEN_OF;

    // Zásadní: Povolení v NVIC, jinak se IRQHandler nikdy nezavolá
	NVIC_ClearPendingIRQ(TIMER1_IRQn);
	NVIC_EnableIRQ(TIMER1_IRQn);

	// Na zaèátku ho necháme stát - spustí ho až první hrana (pocsag.c)
	TIMER1->CMD = TIMER_CMD_STOP;}

void TIMER1_Start(void)
{
    TIMER1->CNT  = 0;
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

void TIMER1_IRQHandler(void) {
    TIMER1->IFC = TIMER_IFC_OF;
//    LED2_Toggle();
    POCSAG_sample_bit(); // Tato funkce nyní øeší èasování
//    GPIO_PinOutToggle(TX_PORT, TX_PIN);
//    GPIO_PinOutToggle(PTT_PORT, PTT_PIN);

}
