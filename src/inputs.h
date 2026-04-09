/******************************************************************************
 *
 *  Obsluha VSTUPNICH SIGNALU
 *
 *****************************************************************************/
#ifndef INPUTS_H
#define INPUTS_H

#include <stdint.h>

//--- Kalibrace rychlosti prijmu
extern bool calib_start;
extern bool calib_stop;
extern uint32_t calib_start_counter;
extern uint32_t calib_stop_counter;
extern uint16_t calib_bits;
extern uint32_t calib_count_per_bit; //-- Pocet tiku na bit (aby to nemusel porad pocitat)

void     initInputs(void);
uint32_t Input_GetRX(void);
uint32_t Input_GetOnBattery(void);
uint32_t Input_GetTamper(void);

void rx_edge_irq_enabled(void);
void rx_edge_irq_disabled(void);

#endif /* INPUTS_H */
