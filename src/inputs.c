/******************************************************************************
 *
 *  Obsluha VSTUPNICH SIGNALU
 *
 *****************************************************************************/
#include "em_gpio.h"
#include "em_cmu.h"
#include "em_timer.h"

#include "inputs.h"
#include "ports.h"
#include "pocsag.h"
#include "timer1.h"
#include "uart1.h"

//--- Stavy alarmu
type_alarm_status tamper_alarm;
type_alarm_status batery_alarm;


//--- Kalibrace rychlosti prijmu
bool calib_start = false;
bool calib_stop = false;
uint32_t calib_start_counter = 0;
uint32_t calib_stop_counter = 0;
uint16_t calib_bits = 0;
uint32_t calib_count_per_bit = 0; //-- Pocet tiku na bit (aby to nemusel porad pocitat)

void initInputs(void) {
    CMU_ClockEnable(cmuClock_GPIO, true);
    /* PA0 - RX vstup s pull-down */
    GPIO_PinModeSet(RX_PORT,        RX_PIN,        gpioModeInputPullFilter, 0);
    /* PA3 - OnBattery vstup s pull-down */
    GPIO_PinModeSet(ONBATTERY_PORT, ONBATTERY_PIN, gpioModeInputPullFilter, 0);
    /* PA4 - Tamper vstup s pull-down */
    GPIO_PinModeSet(TAMPER_PORT,    TAMPER_PIN,    gpioModeInputPullFilter, 0);

    // Povolení NVIC pro externí pøerušení
    NVIC_EnableIRQ(GPIO_EVEN_IRQn); // PA0 je na sudém kanálu
}

uint32_t Input_GetRX(void)         { return GPIO_PinInGet(RX_PORT,        RX_PIN);        }
unsigned char Input_GetOnBattery(void)  { return GPIO_PinInGet(ONBATTERY_PORT, ONBATTERY_PIN); }
unsigned char Input_GetTamper(void)     { return GPIO_PinInGet(TAMPER_PORT,    TAMPER_PIN);    }

//------------------------------------------------------------------------------
// GPIO preruseni od PA0 - detekce hrany POCSAG signalu
//------------------------------------------------------------------------------
void GPIO_EVEN_IRQHandler(void) {
    uint32_t flags = GPIO_IntGet();
    GPIO_IntClear(flags);

    if (flags & (1 << RX_PIN)) {
    	if (calib_start) {
//            TIMER_CounterSet((TIMER_TypeDef *)WTIMER0, 0); // Nuluje counter
    		calib_start_counter = TIMER_CounterGet((TIMER_TypeDef *)WTIMER0);
    		calib_start = false;
    		calib_bits = 1;
    	}
    	else {
    		calib_bits++;   // Bude incrementovat ikdyz nemerime, takze pretece, ale nevadi
    	}

		if (calib_stop) {
			calib_stop_counter = TIMER_CounterGet((TIMER_TypeDef *)WTIMER0);
			calib_count_per_bit = (calib_stop_counter-calib_start_counter)/calib_bits;
			calib_stop = false;
            TIMER1_Calibrate(calib_count_per_bit);
		}

		POCSAG_edge_detected();
    }
}

void rx_edge_irq_enabled(void) {
    GPIO_IntClear(1 << RX_PIN);   // vymazat pending flag PØED povolením
    GPIO_IntEnable(1 << RX_PIN);
}

void rx_edge_irq_disabled(void) {
	GPIO_IntDisable(1 << RX_PIN);
}

//------------------------------------------------------------------------------
// Alarm: TAMPER
//------------------------------------------------------------------------------
void init_tamper_alarm(void) {
	tamper_alarm = NORMAL_CONDITION;
}

type_alarm_status tamper_alarm_status (void) {
	return tamper_alarm;
}

void set_tamper_alarm (type_alarm_status status) {
	tamper_alarm = status;
}

void tamper_alarm_pending(void) {
	switch (tamper_alarm) {
		case NORMAL_CONDITION:
			//-- Vyhlasi alarm
			if (Input_GetTamper()==1) {
				tamper_alarm = ALARM_START;
				sendStringUART1("ALARM: Tamper-start\r\n");
			}
			break;

		case ALARM_START:
			//-- Pokud nestacil vyhlasit, ignoruje ho
			if (Input_GetTamper()==0) {tamper_alarm = NORMAL_CONDITION;}
			//-- Jinak prejde do ALARM_SENT behem vysilani tokenu do ktereho zapsal alarm-start
			break;

		case ALARM_SENT:
			//-- Ukonci alarm
			if (Input_GetTamper()==0) {tamper_alarm = ALARM_END;}
			break;

		case ALARM_END:
			// Pokud doslo k obnoveni alarmu, ktery jeste nezrusil, tak znovu ceka na konec
			if (Input_GetTamper()==1) {tamper_alarm = ALARM_SENT;}
			//-- Jinak prejde do NORMAL_CONDITION behem vysilani tokenu do ktereho zapsal alarm-end
			break;
	}
}

//------------------------------------------------------------------------------
// Alarm: ON BATERY
//------------------------------------------------------------------------------
void init_batery_alarm(void) {
	batery_alarm = NORMAL_CONDITION;
}

type_alarm_status batery_alarm_status (void) {
	return batery_alarm;
}

void set_batery_alarm (type_alarm_status status) {
	batery_alarm = status;
}

void batery_alarm_pending(void) {
	static unsigned char batery_timeout = 0;

	switch (batery_alarm) {
		case NORMAL_CONDITION:
			//-- Vyhlasi alarm (az po prekroceni BATERY_ALARM_TIMEOUT)
			if (Input_GetOnBattery()==1) {
				batery_timeout++;
				if (batery_timeout >= BATERY_ALARM_TIMEOUT)
				{
					batery_alarm = ALARM_START;
					batery_timeout = 0;
					sendStringUART1("ALARM: Batery-start\r\n");
				}
			}
			break;

		case ALARM_START:
			//-- Pokud nestacil vyhlasit, ignoruje ho
			if (Input_GetOnBattery()==0) {batery_alarm = NORMAL_CONDITION;}
			//-- Jinak prejde do ALARM_SENT behem vysilani tokenu do ktereho zapsal alarm-start
			break;

		case ALARM_SENT:
			//-- Ukonci alarm
			if (Input_GetOnBattery()==0) {batery_alarm = ALARM_END;}
			break;

		case ALARM_END:
			// Pokud doslo k obnoveni alarmu, ktery jeste nezrusil, tak znovu ceka na konec
			if (Input_GetOnBattery()==1) {batery_alarm = ALARM_SENT;}
			//-- Jinak prejde do NORMAL_CONDITION behem vysilani tokenu do ktereho zapsal alarm-end
			break;
	}
}

//------------------------------------------------------------------------------
// Vypise stav alarmu
//------------------------------------------------------------------------------
void show_alarm_status(type_alarm_status status) {
	switch (status) {
		case NORMAL_CONDITION:
			sendStringUART1("NORMAL_CONDITION");
			break;

		case ALARM_START:
			sendStringUART1("ALARM_START");
			break;

		case ALARM_SENT:
			sendStringUART1("ALARM_SENT");
			break;

		case ALARM_END:
			sendStringUART1("ALARM_END");
			break;
	}
}
