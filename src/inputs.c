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
    		calib_start_counter = TIMER_CounterGet((TIMER_TypeDef *)WTIMER0);
    		calib_start = false;
    	}

		if (calib_stop) {
			calib_stop_counter = TIMER_CounterGet((TIMER_TypeDef *)WTIMER0);
			calib_stop = false;
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
