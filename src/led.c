/***************************************************************************//**
 * Obsluha LED
 ******************************************************************************/
#include "led.h"
#include "ports.h"
#include "em_gpio.h"
#include "em_cmu.h"

void initLED(void) {
    CMU_ClockEnable(cmuClock_GPIO, true);
    GPIO_PinModeSet(LED1_PORT,   LED1_PIN,   gpioModePushPull, 0);
    GPIO_PinModeSet(LED2_PORT,   LED2_PIN,   gpioModePushPull, 0);
    GPIO_PinModeSet(LED3_PORT,   LED3_PIN,   gpioModePushPull, 0);
    GPIO_PinModeSet(LED4_PORT,   LED4_PIN,   gpioModePushPull, 0);
    GPIO_PinModeSet(LED_RX_PORT, LED_RX_PIN, gpioModePushPull, 0);
    GPIO_PinModeSet(LED_TX_PORT, LED_TX_PIN, gpioModePushPull, 0);
}

void LED1_On(void)     { GPIO_PinOutSet(LED1_PORT, LED1_PIN);       }
void LED1_Off(void)    { GPIO_PinOutClear(LED1_PORT, LED1_PIN);     }
void LED1_Toggle(void) { GPIO_PinOutToggle(LED1_PORT, LED1_PIN);    }

void LED2_On(void)     { GPIO_PinOutSet(LED2_PORT, LED2_PIN);       }
void LED2_Off(void)    { GPIO_PinOutClear(LED2_PORT, LED2_PIN);     }
void LED2_Toggle(void) { GPIO_PinOutToggle(LED2_PORT, LED2_PIN);    }

void LED3_On(void)     { GPIO_PinOutSet(LED3_PORT, LED3_PIN);       }
void LED3_Off(void)    { GPIO_PinOutClear(LED3_PORT, LED3_PIN);     }
void LED3_Toggle(void) { GPIO_PinOutToggle(LED3_PORT, LED3_PIN);    }

void LED4_On(void)     { GPIO_PinOutSet(LED4_PORT, LED4_PIN);       }
void LED4_Off(void)    { GPIO_PinOutClear(LED4_PORT, LED4_PIN);     }
void LED4_Toggle(void) { GPIO_PinOutToggle(LED4_PORT, LED4_PIN);    }

void LED_RX_On(void)     { GPIO_PinOutSet(LED_RX_PORT, LED_RX_PIN);    }
void LED_RX_Off(void)    { GPIO_PinOutClear(LED_RX_PORT, LED_RX_PIN);  }
void LED_RX_Toggle(void) { GPIO_PinOutToggle(LED_RX_PORT, LED_RX_PIN); }

void LED_TX_On(void)     { GPIO_PinOutSet(LED_TX_PORT, LED_TX_PIN);    }
void LED_TX_Off(void)    { GPIO_PinOutClear(LED_TX_PORT, LED_TX_PIN);  }
void LED_TX_Toggle(void) { GPIO_PinOutToggle(LED_TX_PORT, LED_TX_PIN); }
