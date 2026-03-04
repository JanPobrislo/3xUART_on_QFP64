/******************************************************************************
 * @file uart0.c  COM-A
 * @brief Obsluha UART0 - 9600 8N1, LOCATION 4 (PC4/PC5)
 *****************************************************************************/
#include "uart0.h"
#include "usart0.h"   /* USART_BaudrateSet_Manual */
#include "ports.h"
#include "em_usart.h"
#include "em_cmu.h"
#include "em_gpio.h"

char     rxBuffer2[BUFFER_SIZE];
volatile uint16_t rxIndex2 = 0;

void initUART0(void)
{
    CMU_ClockEnable(cmuClock_UART0, true);
    CMU_ClockEnable(cmuClock_GPIO,  true);

    GPIO_PinModeSet(gpioPortC, 4, gpioModePushPull, 1); /* TX */
    GPIO_PinModeSet(gpioPortC, 5, gpioModeInput,    0); /* RX */

    UART0->CMD = USART_CMD_RXDIS | USART_CMD_TXDIS | USART_CMD_MASTERDIS
               | USART_CMD_RXBLOCKDIS | USART_CMD_TXTRIDIS
               | USART_CMD_CLEARTX | USART_CMD_CLEARRX;

    UART0->CTRL  = USART_CTRL_OVS_X16;
    UART0->FRAME = USART_FRAME_DATABITS_EIGHT
                 | USART_FRAME_PARITY_NONE
                 | USART_FRAME_STOPBITS_ONE;

    USART_BaudrateSet_Manual(UART0, 9600, HFCLK_FREQ);

    UART0->ROUTEPEN  = USART_ROUTEPEN_RXPEN | USART_ROUTEPEN_TXPEN;
    UART0->ROUTELOC0 = (4 << _USART_ROUTELOC0_TXLOC_SHIFT)
                     | (4 << _USART_ROUTELOC0_RXLOC_SHIFT);

    UART0->CMD = USART_CMD_RXEN | USART_CMD_TXEN;

    USART_IntClear(UART0, _USART_IF_MASK);
    USART_IntEnable(UART0, USART_IEN_RXDATAV);
    NVIC_ClearPendingIRQ(UART0_RX_IRQn);
    NVIC_EnableIRQ(UART0_RX_IRQn);
}

void sendStringUART0(const char *str)
{
    while (*str) { USART_Tx(UART0, *str++); }
}

void UART0_RX_IRQHandler(void)
{
    uint8_t data = USART_Rx(UART0);
    if (data == 13) {
        rxBuffer2[rxIndex2] = '\0';
        sendStringUART0(rxBuffer2);
        sendStringUART0("\r\n");
        rxIndex2 = 0;
    } else {
        if (rxIndex2 < BUFFER_SIZE - 1) rxBuffer2[rxIndex2++] = data;
    }
}
