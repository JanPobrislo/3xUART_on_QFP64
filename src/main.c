/***************************************************************************//**
 * @file main.c  funkční pro EFM32GG11B820F2048GQ64
 * @brief Program pro obsluhu tří RS232 portů a čtyř LED diod na EFM32GG11B820F2048GQ64
 * @version 3.6
 * @note Upraven pro QFP64 balíček s LED1 na PA8, LED2 na PD5, LED3 na PD6, LED4 na PD8
 * @note LED3 zobrazuje stav vstupu OnBattery (PA3)
 * @note LED4 zobrazuje stav vstupu Tamper (PA4)
 * @note TIMER0 (1 Hz) bliká LED2, TIMER1 (1200 Hz) toggleuje TX a posílá '*' na UART1
 * @note Upraven pro 72 MHz (DPLL: HFXO 50 MHz × 3600 / 2500)
 *
 * @brief Program pro obsluhu tri RS232 portu a LED diod
 * @version 4.0 - rozdeleno do modulu
 *****************************************************************************/
#include <stdio.h>

#include "em_device.h"
#include "em_chip.h"
#include "em_cmu.h"
#include "em_gpio.h"
#include "em_usart.h"

#include "ports.h"
#include "parameters.h"
#include "timer0.h"
#include "timer1.h"
#include "led.h"
#include "inputs.h"
#include "usart0.h"
#include "uart0.h"
#include "uart1.h"
#include "pocsag.h"

//------------------------------------------------------------------------------
//  Globalni promenne
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
//  Init procesoru
//------------------------------------------------------------------------------
static void initClocks(void)
{
    CMU_HFXOInit_TypeDef hfxoInit = CMU_HFXOINIT_DEFAULT;
    hfxoInit.mode = cmuOscMode_Crystal;
    CMU_HFXOInit(&hfxoInit);
    CMU_OscillatorEnable(cmuOsc_HFXO, true, true);

    CMU_DPLLInit_TypeDef dpllInit = {
        .frequency   = 72000000UL,
        .n           = 3599,
        .m           = 2499,
        .refClk      = cmuSelect_HFXO,
        .edgeSel     = cmuDPLLEdgeSel_Fall,
        .lockMode    = cmuDPLLLockMode_Freq,
        .autoRecover = true
    };

    if (!CMU_DPLLLock(&dpllInit))
        CMU_ClockSelectSet(cmuClock_HF, cmuSelect_HFXO);

    CMU->HFPERPRESC = (CMU->HFPERPRESC & ~_CMU_HFPERPRESC_PRESC_MASK);
    CMU_ClockEnable(cmuClock_HFPER, true);
}

static void initOutputs(void)
{
    CMU_ClockEnable(cmuClock_GPIO, true);
    GPIO_PinModeSet(TX_PORT,  TX_PIN,  gpioModePushPull, 0);
    GPIO_PinModeSet(PTT_PORT, PTT_PIN, gpioModePushPull, 0);
    GPIO_PinModeSet(DBG_PORT, DBG_PIN, gpioModePushPull, 0);

	GPIO_PinOutSet(PTT_PORT, PTT_PIN);
}

static void delay_ms(uint32_t ms)
{
    volatile uint32_t i;
    for (i = 0; i < 2571 * ms; i++) { i = i; }
}

//------------------------------------------------------------------------------
// GPIO preruseni pro PA0 - detekce hrany POCSAG signalu
//------------------------------------------------------------------------------
void GPIO_EVEN_IRQHandler(void) {
    uint32_t flags = GPIO_IntGet();
    GPIO_IntClear(flags);
    if (flags & (1 << RX_PIN)) {
        POCSAG_edge_detected();
    }
}

//------------------------------------------------------------------------------
//
//                           M A I N
//
//------------------------------------------------------------------------------
int main(void)
{
	char txt[250] = "";

    CHIP_Init();
    initClocks();       /* MUSI BYT PRVNI */
    Inputs_Init();
    initOutputs();
    LED_Init();

    initUSART0();
    initUART0();
    initUART1();
    initTIMER0();
    initTIMER1();

    //----------------- Blikačka
    LED1_On(); delay_ms(300); LED1_Off();
    LED2_On(); delay_ms(300); LED2_Off();
    LED3_On(); delay_ms(300); LED3_Off();
    LED4_On(); delay_ms(300); LED4_Off();
    LED_RX_On(); delay_ms(300); LED_RX_Off();
    LED_TX_On(); delay_ms(300); LED_TX_Off();

    Parameters_Init();
    POCSAG_rx_init();

    for (volatile int i = 0; i < 100000; i++);

    GPIO_PinOutClear(DBG_PORT, DBG_PIN);

    //----------------- Hlavicky
    sendStringUART0 ("TCI COM1-A (UART0)\r\n");
    sendStringUSART0("TCI COM3-C (USART0)\r\n");

    sendStringUART1 ("TCI COM2-B (UART1) - DEBUG\r\n");
    sendStringUART1 ("HFXO: 50 MHz krystal, DPLL: 72 MHz\r\n");
    Parameters_Show();

    while (1) {

    	//------------------------------------------------------------------------------
    	//  Prijem POCSAG
    	//------------------------------------------------------------------------------
    	POCSAG_process(); // Zpracuje a vypíše datagram, pokud je připraven

    	//------------------------------------------------------------------------------
    	//  Prikaz z COM-B (UART1)
    	//------------------------------------------------------------------------------
    	if (tci_cmd != 0)
    	{
    		switch (tci_cmd)
    		{
    		case 'h' : 	sendStringUART1("help\r\n");
    					sendStringUART1(" --------------------------------\r\n");
    					sendStringUART1(" TCI commands:\r\n");
    					sendStringUART1(" --------------------------------\r\n");
    					sendStringUART1(" 1..6 : Toggle LED\r\n");
    					sendStringUART1(" t : start TX TOKEN\r\n");
    					sendStringUART1(" T : stop timer1 1200Hz\r\n");
    					sendStringUART1(" x : GPIO_IntEnable(RX_PIN)\r\n");
    					sendStringUART1(" h : display this help\r\n");
    					sendStringUART1(" --------------------------------\r\n");
    					POCSAG_show_rx_state();
    					sendStringUART1(" --------------------------------\r\n");
						break;
    		case '1' : 	LED1_Toggle();
    	    			GPIO_PinOutToggle(DBG_PORT, DBG_PIN);
    					sendStringUART1("LED1");
    					break;
    		case '2' : 	LED2_Toggle();
    					sendStringUART1("LED2");
    					break;
    		case '3' : 	LED3_Toggle();
    					sendStringUART1("LED3");
    					break;
    		case '4' : 	LED4_Toggle();
    					sendStringUART1("LED4");
    					break;
    		case '5' : 	LED_RX_Toggle();
    					sendStringUART1("LED RX");
    					break;
    		case '6' : 	LED_TX_Toggle();
    					sendStringUART1("LED TX");
    					break;
    		case 't' : 	tx_start();
    					sendStringUART1("Tx datagram\r\n");
    					break;
    		case 'T' : 	TIMER1_Stop();
    					sendStringUART1("Stop timer1 TX 1200Hz\r\n");
    					break;
    		case 'x' : 	sendStringUART1("TX toggle bit\r\n");
//    					GPIO_PinOutToggle(TX_PORT, TX_PIN);
						GPIO_IntEnable(1 << RX_PIN);
    					break;
    		default:	break;
    		}

    		sendStringUART1("\r\nTCI> ");
    		tci_cmd=0;
    	}

    	//------------------------------------------------------------------------------
    	//  Akce 1x za vterinu
    	//------------------------------------------------------------------------------
    	if (IsSecond==1)
    	{
    		IsSecond=0;

    		/*
    		if (Input_GetOnBattery()) {
				sendStringUART1("Batery:1  ");
				LED3_On();
			} else {
				sendStringUART1("Batery:0  ");
				LED3_Off();
			}

			if (Input_GetTamper()) {
				sendStringUART1("Tamper:1  ");
				LED4_On();
			} else {
				sendStringUART1("Tamper:0  ");
				LED4_Off();
			}
			sprintf(txt,"Time: %lu  ",SecondCounter);
			sendStringUART1(txt);
			sendStringUART1("\r\n");
			*/
//    	    sendStringUART0 ("[COM1-A]");
//    	    sendStringUART1 ("[COM2-B]");
//    	    sendStringUSART0("[COM3-C]");

	    }
    }
}
