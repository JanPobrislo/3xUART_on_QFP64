/******************************************************************************
 * @file uart1.c  COM-B
 * @brief Obsluha UART1 - 115200 8N1, LOCATION 4 (PE12/PE13)
 *****************************************************************************/
#ifndef UART1_H
#define UART1_H

#include <stdint.h>

void initUART1(void);
void sendCharUART1(char zn);
void sendStringUART1(const char *str);

extern char     rxBuffer3[];
extern volatile uint16_t rxIndex3;

extern char     tci_cmd;

#endif /* UART1_H */
