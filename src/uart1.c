/******************************************************************************
 * @file uart1.c  COM-B
 * @brief Obsluha UART1 - 115200 8N1, LOCATION 4 (PE12/PE13)
 *****************************************************************************/
#include "uart1.h"
#include "usart0.h"   /* USART_BaudrateSet_Manual */
#include "ports.h"
#include "em_usart.h"
#include "em_cmu.h"
#include "em_gpio.h"

char     rxBuffer3[BUFFER_SIZE];
volatile uint16_t rxIndex3 = 0;
char     tci_cmd;

void initUART1(void)
{
    CMU_ClockEnable(cmuClock_UART1, true);
    CMU_ClockEnable(cmuClock_GPIO,  true);

    GPIO_PinModeSet(gpioPortE, 12, gpioModePushPull, 1); /* TX */
    GPIO_PinModeSet(gpioPortE, 13, gpioModeInput,    0); /* RX */

    UART1->CMD = USART_CMD_RXDIS | USART_CMD_TXDIS | USART_CMD_MASTERDIS
               | USART_CMD_RXBLOCKDIS | USART_CMD_TXTRIDIS
               | USART_CMD_CLEARTX | USART_CMD_CLEARRX;

    UART1->CTRL  = USART_CTRL_OVS_X16;
    UART1->FRAME = USART_FRAME_DATABITS_EIGHT
                 | USART_FRAME_PARITY_NONE
                 | USART_FRAME_STOPBITS_ONE;

    USART_BaudrateSet_Manual(UART1, 115200, HFCLK_FREQ);

    UART1->ROUTEPEN  = USART_ROUTEPEN_RXPEN | USART_ROUTEPEN_TXPEN;
    UART1->ROUTELOC0 = (4 << _USART_ROUTELOC0_TXLOC_SHIFT)
                     | (4 << _USART_ROUTELOC0_RXLOC_SHIFT);

    UART1->CMD = USART_CMD_RXEN | USART_CMD_TXEN;

    USART_IntClear(UART1, _USART_IF_MASK);
    USART_IntEnable(UART1, USART_IEN_RXDATAV);
    NVIC_ClearPendingIRQ(UART1_RX_IRQn);
    NVIC_EnableIRQ(UART1_RX_IRQn);

    tci_cmd = 0;
}

void sendCharUART1(char zn)
{
    USART_Tx(UART1, (uint8_t)zn);
}

void sendStringUART1(const char *str)
{
    while (*str) { USART_Tx(UART1, *str++); }
}

void UART1_RX_IRQHandler(void)
{
	uint8_t data = USART_Rx(UART1);
    if (data == 13) {
        rxBuffer3[rxIndex3] = '\0';
        //sendStringUART1(rxBuffer3);
        sendStringUART1("\r\nTCI> ");
        if (rxIndex3 < BUFFER_SIZE - 1)
        {  	tci_cmd = rxBuffer3[rxIndex3-1];
        }
        rxIndex3 = 0;
    } else {
    	sendCharUART1(data);
        if (rxIndex3 < BUFFER_SIZE - 1) rxBuffer3[rxIndex3++] = data;
    }
}
