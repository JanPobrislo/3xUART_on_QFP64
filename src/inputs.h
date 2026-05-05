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
unsigned char Input_GetOnBattery(void);
unsigned char Input_GetTamper(void);

void rx_edge_irq_enabled(void);
void rx_edge_irq_disabled(void);


typedef enum {
	NORMAL_CONDITION,
	ALARM_START,
	ALARM_SENT,
	ALARM_END
} type_alarm_status;

#define BATERY_ALARM_TIMEOUT 240

void init_tamper_alarm(void);
type_alarm_status tamper_alarm_status (void);
void set_tamper_alarm (type_alarm_status status);
void tamper_alarm_pending(void);

void init_batery_alarm(void);
type_alarm_status batery_alarm_status (void);
void set_batery_alarm (type_alarm_status status);
void batery_alarm_pending(void);

void show_alarm_status(type_alarm_status status);

#endif /* INPUTS_H */
