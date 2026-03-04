/******************************************************************************
 *
 * Definice vsech vstupnich a vystupnich portu pro EFM32GG11B820F2048GQ64
 *
 *****************************************************************************/
#ifndef PORTS_H
#define PORTS_H

#include "em_gpio.h"

/* --- Vystupni porty --- */
#define TX_PORT             (gpioPortA)
#define TX_PIN              (1)

#define PTT_PORT            (gpioPortA)
#define PTT_PIN             (2)

/* --- Vstupni porty --- */
#define RX_PORT             (gpioPortA)
#define RX_PIN              (0)

#define ONBATTERY_PORT      (gpioPortA)
#define ONBATTERY_PIN       (3)

#define TAMPER_PORT         (gpioPortA)
#define TAMPER_PIN          (4)

/* --- LED diody --- */
#define LED1_PORT           (gpioPortA)
#define LED1_PIN            (8)

#define LED2_PORT           (gpioPortD)
#define LED2_PIN            (5)

#define LED3_PORT           (gpioPortD)
#define LED3_PIN            (6)

#define LED4_PORT           (gpioPortD)
#define LED4_PIN            (8)

#define LED_RX_PORT         (gpioPortD)
#define LED_RX_PIN          (2)

#define LED_TX_PORT         (gpioPortD)
#define LED_TX_PIN          (3)

/* --- Frekvencni konstanty --- */
#define HFXO_FREQ           50000000UL
#define HFCLK_FREQ          72000000UL

/* --- Velikost bufferu --- */
#define BUFFER_SIZE         256

#endif /* PORTS_H */
