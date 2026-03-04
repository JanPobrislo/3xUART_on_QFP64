/******************************************************************************
 * @file usart0.c COM-C
 * @brief Obsluha USART0 - 115200 8N1, LOCATION 0 (PE10/PE11)
 *****************************************************************************/
#ifndef USART0_H
#define USART0_H

#include <stdint.h>
#include "em_usart.h"   /* USART_TypeDef */

void initUSART0(void);
void sendStringUSART0(const char *str);
void USART_BaudrateSet_Manual(USART_TypeDef *usart, uint32_t baudrate, uint32_t freq);

extern char              rxBuffer1[];
extern volatile uint16_t rxIndex1;

#endif /* USART0_H */
