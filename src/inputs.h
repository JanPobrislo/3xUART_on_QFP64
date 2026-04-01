/******************************************************************************
 *
 *  Obsluha VSTUPNICH SIGNALU
 *
 *****************************************************************************/
#ifndef INPUTS_H
#define INPUTS_H

#include <stdint.h>

void     initInputs(void);
uint32_t Input_GetRX(void);
uint32_t Input_GetOnBattery(void);
uint32_t Input_GetTamper(void);

void rx_edge_irq_enabled(void);
void rx_edge_irq_disabled(void);

#endif /* INPUTS_H */
