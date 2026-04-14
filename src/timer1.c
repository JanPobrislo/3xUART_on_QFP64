/******************************************************************************
 * @file timer1.c
 * @brief Obsluha TIMER1 – 1200 Hz pro POCSAG vzorkování
 *
 * @note HFCLK = 72 MHz, PRESC = DIV16, TOP = 3750
 *       72 000 000 / 16 / 3750 = 1200 Hz
 *
 * PREDELANO:
 * 		Zrusen PRESC (=1) aby citac cital az do 60.000 stejne jako WTIMER0
 * 		72 000 000 / 1 / 1200 = 60000
 * 		To zarucuje co nejpresnejsi kalibraci rychlosti.
 * 		Co bylo namereno bude i nastaveno.
 *
 * TIMER1 taktuje POCSAG_SampleBit() 1200x za sekundu.
 * Uvnitø POCSAG_SampleBit() se odeète RX
 * Støed bitu - Synchronizaci fáze zajišuje POCSAG_edge_detected() volaná
 * z GPIO_EVEN_IRQHandler pøi hranì signálu na PA0.
 *****************************************************************************/
#include "timer1.h"
#include "ports.h"
#include "pocsag.h"
#include "led.h"

#include "em_cmu.h"
#include "em_timer.h"
#include "em_gpio.h"

void initTIMER1(void)
{
    CMU_ClockEnable(cmuClock_TIMER1, true);

    TIMER1->CTRL = 0;
    TIMER1->CNT  = 0;
    TIMER1->TOP  = TIMER1_TOP;
//    TIMER1->CTRL = TIMER_CTRL_PRESC_DIV16 | TIMER_CTRL_MODE_UP;
    TIMER1->CTRL = TIMER_CTRL_PRESC_DIV1 | TIMER_CTRL_MODE_UP;
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
    LED2_Toggle();
    LED_TX_Off();
    POCSAG_sample_bit(); // Tato funkce øeší RX/TX jednoho bitu.
//    GPIO_PinOutToggle(DBG_PORT, DBG_PIN);

}

//------------------------------------------------------------------------------
// Kalibrace - nastavi TOP podle namerene rychlosti v preamble
// Rychlost se meri pomoci wtimer0 - 72MHz t.j. pro 1200Hz nacita 60000x
// TOP = (72M / f) -1
// pro f=1200Hz to je 60000-1
// pri vzorkovani 72MHz je TOP kalibrovane s presnosti +/- 13,88nsec
//------------------------------------------------------------------------------
void TIMER1_Calibrate(uint32_t calib_counter)
{
	//-- Ochrana, kalibrujeme jen pri odchylce +/- 24Hz (2%) t.j. <1176,1224>
	if (calib_counter>58823 && calib_counter<61177) {
//		TIMER1->TOP = ((calib_counter/16)+0.5)-1;
		TIMER1->TOP = calib_counter - 1;
	}
}

void TIMER1_ResetSpeed(void)
{
    TIMER1->TOP  = TIMER1_TOP;
}
