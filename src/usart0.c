/******************************************************************************
 * @file usart0.c COM-C
 * @brief Obsluha USART0 - 115200 8N1, LOCATION 0 (PE10/PE11)
 *****************************************************************************/
#include "usart0.h"
#include "ports.h"
#include "em_usart.h"
#include "em_cmu.h"
#include "em_gpio.h"

char     rxBuffer1[BUFFER_SIZE];
volatile uint16_t rxIndex1 = 0;

void USART_BaudrateSet_Manual(USART_TypeDef *usart, uint32_t baudrate, uint32_t freq)
{
    uint32_t oversample = 16;
    uint32_t clkdiv = (((freq * 4) / (baudrate * oversample)) - 4) << 6;
    usart->CLKDIV = clkdiv;
}

void initUSART0(void)
{
    CMU_ClockEnable(cmuClock_USART0, true);
    CMU_ClockEnable(cmuClock_GPIO,   true);

    GPIO_PinModeSet(gpioPortE, 10, gpioModePushPull, 1); /* TX */
    GPIO_PinModeSet(gpioPortE, 11, gpioModeInput,    0); /* RX */

    USART0->CMD = USART_CMD_RXDIS | USART_CMD_TXDIS | USART_CMD_MASTERDIS
                | USART_CMD_RXBLOCKDIS | USART_CMD_TXTRIDIS
                | USART_CMD_CLEARTX | USART_CMD_CLEARRX;

    USART0->CTRL  = USART_CTRL_OVS_X16;
    USART0->FRAME = USART_FRAME_DATABITS_EIGHT
                  | USART_FRAME_PARITY_NONE
                  | USART_FRAME_STOPBITS_ONE;

    USART_BaudrateSet_Manual(USART0, 115200, HFCLK_FREQ);

    USART0->ROUTEPEN  = USART_ROUTEPEN_RXPEN | USART_ROUTEPEN_TXPEN;
    USART0->ROUTELOC0 = (0 << _USART_ROUTELOC0_TXLOC_SHIFT)
                      | (0 << _USART_ROUTELOC0_RXLOC_SHIFT);

    USART0->CMD = USART_CMD_RXEN | USART_CMD_TXEN;

    USART_IntClear(USART0, _USART_IF_MASK);
    USART_IntEnable(USART0, USART_IEN_RXDATAV);
    NVIC_ClearPendingIRQ(USART0_RX_IRQn);
    NVIC_EnableIRQ(USART0_RX_IRQn);
}

void sendStringUSART0(const char *str)
{
    while (*str) { USART_Tx(USART0, *str++); }
}

void USART0_RX_IRQHandler(void)
{
    uint8_t data = USART_Rx(USART0);
    if (data == 13) {
        rxBuffer1[rxIndex1] = '\0';
        sendStringUSART0(rxBuffer1);
        sendStringUSART0("\r\n");
        rxIndex1 = 0;
    } else {
        if (rxIndex1 < BUFFER_SIZE - 1) rxBuffer1[rxIndex1++] = data;
    }
}
