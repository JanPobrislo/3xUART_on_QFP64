/******************************************************************************
 *
 *  Obsluha VSTUPNICH SIGNALU
 *
 *****************************************************************************/
#include "inputs.h"
#include "ports.h"
#include "em_gpio.h"
#include "em_cmu.h"

void Inputs_Init(void) {
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
