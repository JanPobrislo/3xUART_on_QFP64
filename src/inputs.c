/******************************************************************************
 *
 *  Obsluha VSTUPNICH SIGNALU
 *
 *****************************************************************************/
#include "em_gpio.h"
#include "em_cmu.h"
#include "em_timer.h"

#include "inputs.h"
#include "ports.h"
#include "pocsag.h"
#include "timer1.h"

//--- Kalibrace rychlosti prijmu
bool calib_start = false;
bool calib_stop = false;
uint32_t calib_start_counter = 0;
uint32_t calib_stop_counter = 0;
uint16_t calib_bits = 0;
uint32_t calib_count_per_bit = 0; //-- Pocet tiku na bit (aby to nemusel porad pocitat)

void initInputs(void) {
    CMU_ClockEnable(cmuClock_GPIO, true);
    /* PA0 - RX vstup s pull-down */
    GPIO_PinModeSet(RX_PORT,        RX_PIN,        gpioModeInputPullFilter, 0);
    /* PA3 - OnBattery vstup s pull-down */
    GPIO_PinModeSet(ONBATTERY_PORT, ONBATTERY_PIN, gpioModeInputPullFilter, 0);
    /* PA4 - Tamper vstup s pull-down */
    GPIO_PinModeSet(TAMPER_PORT,    TAMPER_PIN,    gpioModeInputPullFilter, 0);

    // Povolení NVIC pro externí pøerušení
    NVIC_EnableIRQ(GPIO_EVEN_IRQn); // PA0 je na sudém kanálu
}

uint32_t Input_GetRX(void)         { return GPIO_PinInGet(RX_PORT,        RX_PIN);        }
uint32_t Input_GetOnBattery(void)  { return GPIO_PinInGet(ONBATTERY_PORT, ONBATTERY_PIN); }
uint32_t Input_GetTamper(void)     { return GPIO_PinInGet(TAMPER_PORT,    TAMPER_PIN);    }

//------------------------------------------------------------------------------
// GPIO preruseni od PA0 - detekce hrany POCSAG signalu
//------------------------------------------------------------------------------
void GPIO_EVEN_IRQHandler(void) {
    uint32_t flags = GPIO_IntGet();
    GPIO_IntClear(flags);

    if (flags & (1 << RX_PIN)) {
    	if (calib_start) {
//            TIMER_CounterSet((TIMER_TypeDef *)WTIMER0, 0); // Nuluje counter
    		calib_start_counter = TIMER_CounterGet((TIMER_TypeDef *)WTIMER0);
    		calib_start = false;
    	}

		if (calib_stop) {
			calib_stop_counter = TIMER_CounterGet((TIMER_TypeDef *)WTIMER0);
			calib_count_per_bit = (calib_stop_counter-calib_start_counter)/calib_bits;
			calib_stop = false;
            TIMER1_Calibrate(calib_count_per_bit);
		}

		POCSAG_edge_detected();
    }
}

void rx_edge_irq_enabled(void) {
	GPIO_IntEnable(1 << RX_PIN);
}

void rx_edge_irq_disabled(void) {
	GPIO_IntDisable(1 << RX_PIN);
}
