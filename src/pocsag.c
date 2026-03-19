#include <stdio.h>
#include "pocsag.h"
#include "inputs.h"
#include "uart1.h"
#include "em_timer.h"
#include "em_gpio.h"
#include "ports.h"
#include "led.h"

typedef enum {
    STATE_IDLE,         // Čekání na preambuli v šumu
    STATE_SYNC_WAIT,    // Preambule nalezena, čekáme na první Sync Word
    STATE_RECEIVING     // Pevné časování, příjem datových slov
} POCSAG_State;

static volatile POCSAG_State state = STATE_IDLE;
static volatile uint32_t shiftReg = 0;
static volatile uint16_t bitCounter = 0;
volatile POCSAG_Message currentMsg;

// --- BCH (31,21) a Parita ---
static uint32_t calculate_syndrom(uint32_t word) {
    uint32_t rem = (word >> 11) << 10;
    rem |= (word >> 1) & 0x3FF;
    for (int i = 0; i < 21; i++) {
        if (rem & (1UL << (30 - i))) rem ^= (0x3B3UL << (20 - i));
    }
    return rem & 0x3FF;
}

static bool check_parity(uint32_t word) {
    uint32_t p = 0;
    for (int i = 0; i < 32; i++) if (word & (1UL << i)) p++;
    return (p % 2 == 0);
}

static uint32_t try_fix_word(uint32_t word, bool *fixed) {
    *fixed = false;
    if (calculate_syndrom(word) == 0 && check_parity(word)) return word;
    for (int i = 0; i < 32; i++) {
        uint32_t test = word ^ (1UL << i);
        if (calculate_syndrom(test) == 0 && check_parity(test)) {
            *fixed = true; return test;
        }
    }
    return word;
}

void POCSAG_Init(void) {
    state = STATE_IDLE;
    currentMsg.ready = false;
    currentMsg.total_words = 0;
    TIMER1->CMD = TIMER_CMD_STOP;
    // Povolení přerušení od hran PA0
    GPIO_ExtIntConfig(RX_PORT, RX_PIN, RX_PIN, true, true, true);
}

// Voláno z GPIO_EVEN_IRQHandler
void POCSAG_EdgeDetected(void) {
    if (state != STATE_RECEIVING) {
    	// Zastavit, pokud běžel (kvůli re-synchronizaci v šumu)
		TIMER1->CMD = TIMER_CMD_STOP;
		// Resetujeme časovač na polovinu periody, aby první Sample přišel do středu bitu
        TIMER1->CNT = TIMER1->TOP / 2;
        TIMER1->CMD = TIMER_CMD_START;
    }
}

// Voláno z TIMER1_IRQHandler (1200 Hz)
void POCSAG_SampleBit(void) {
    static uint8_t wordsInBatch = 0; // Sleduje pozici v rámci aktuálního batche (0-15)

	uint8_t bit = (Input_GetRX() > 0) ? 1 : 0;
    shiftReg = (shiftReg << 1) | bit;

    //-- Diagnostika
    if (bit==1) {GPIO_PinOutSet(PTT_PORT, PTT_PIN);}
    else {GPIO_PinOutClear(PTT_PORT, PTT_PIN);}

    switch (state) {
        case STATE_IDLE:
			// Hledáme střídavou sekvenci (01010101...) v posledních 16 bitech
			// 0xAAAA je 1010101010101010, 0x5555 je 0101010101010101
			if ((uint16_t)(shiftReg & 0xFFFF) == 0xAAAA || (uint16_t)(shiftReg & 0xFFFF) == 0x5555) {
				state = STATE_SYNC_WAIT;
//            	LED1_On();
            }
            break;

        case STATE_SYNC_WAIT:
            if (shiftReg == POCSAG_SYNC_WORD) {
                state = STATE_RECEIVING;
                bitCounter = 0;
                wordsInBatch = 1; // Dalších 16 slov jsou data
                currentMsg.total_words = 0;
                GPIO_IntDisable(1 << RX_PIN); // VYPNEME HRANY - teď už jen pevný čas
            	LED1_On();
//                GPIO_PinOutSet(DBG_PORT, DBG_PIN);
            }
            break;

			case STATE_RECEIVING:
				bitCounter++;
				if (bitCounter >= 32) {
					bitCounter = 0;

					// Právě jsme dočetli 32. bit. Obsah je v shiftReg.
	            	LED3_On();
	                GPIO_PinOutSet(DBG_PORT, DBG_PIN);


					// SCÉNÁŘ A: Čekáme na SYNC slovo (každých 17. slovo v proudu dat)
					if (wordsInBatch == 0) {
						if (shiftReg == POCSAG_SYNC_WORD) {
							// V pořádku, začíná další batch
							// wordsInBatch necháme na 0, ale nepíšeme SYNC do dat
							// (Teoreticky zde wordsInBatch nastavíme na 1 po inkrementaci níže)
						} else {
							// KONEC DATAGRAMU: Na místě, kde měl být SYNC, je něco jiného
							currentMsg.ready = true;
							state = STATE_IDLE;
							TIMER1->CMD = TIMER_CMD_STOP;
							GPIO_IntEnable(1 << RX_PIN);
							LED3_Off();
			            	LED1_On();
							return;
						}
					}

					// SCÉNÁŘ B: Čteme datové slovo (1-16)
					else {
						if (currentMsg.total_words < (MAX_BATCHES * WORDS_PER_BATCH)) {
							currentMsg.data[currentMsg.total_words++] = shiftReg;
						}
					}

					// Inkrementace a reset čítače batche (0-16, kde 0 je pozice pro SYNC)
					wordsInBatch++;
					if (wordsInBatch > 16) {
						wordsInBatch = 0; // Příští slovo MUSÍ být SYNC
					}

					// Ochrana proti přetečení celkového pole
					if (currentMsg.total_words >= (MAX_BATCHES * WORDS_PER_BATCH)) {
						currentMsg.ready = true;
						state = STATE_IDLE;
						TIMER1->CMD = TIMER_CMD_STOP;
						GPIO_IntEnable(1 << RX_PIN);
					}
				}
				break;
    }
}

void POCSAG_Process(void) {
    if (!currentMsg.ready) return;

    char buf[128];
    sendStringUART1("\r\n>>> POCSAG MSG START <<<\r\n");

    for (uint16_t i = 0; i < currentMsg.total_words; i++) {
        bool fixed = false;
        uint32_t raw = currentMsg.data[i];

        if (raw == POCSAG_IDLE_WORD)
        {
            sprintf(buf, "W[%03d]: IDLE\r\n", i);
            sendStringUART1(buf);
        	continue;
        }

        uint32_t clean = try_fix_word(raw, &fixed);
        bool valid = (calculate_syndrom(clean) == 0 && check_parity(clean));

        sprintf(buf, "W[%03d]: %08X %s %s\r\n",
                i, (unsigned int)clean,
                valid ? "OK" : "ERR", fixed ? "[FIXED]" : "");
        sendStringUART1(buf);
    }

    sendStringUART1(">>> MSG END <<<\r\nTC1> ");
    currentMsg.ready = false;
}
