/******************************************************************************
 *
 *  Obsluha VSTUPNICH SIGNALU
 *
 *****************************************************************************/
#ifndef INPUTS_H
#define INPUTS_H

#include <stdint.h>

void     Inputs_Init(void);
uint32_t Input_GetRX(void);
uint32_t Input_GetOnBattery(void);
uint32_t Input_GetTamper(void);

#endif /* INPUTS_H */
