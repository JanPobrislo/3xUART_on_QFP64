/******************************************************************************
 * @file uart0.c  COM-A
 * @brief Obsluha UART0 - 9600 8N1, LOCATION 4 (PC4/PC5)
 *****************************************************************************/
#ifndef UART0_H
#define UART0_H

#include <stdint.h>

void initUART0(void);
void sendStringUART0(const char *str);

extern char     rxBuffer2[];
extern volatile uint16_t rxIndex2;

#endif /* UART0_H */
