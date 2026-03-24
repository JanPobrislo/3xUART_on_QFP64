#include <stdio.h>
#include <string.h>
#include "pocsag.h"
#include "inputs.h"
#include "uart1.h"
#include "em_timer.h"
#include "em_gpio.h"
#include "ports.h"
#include "led.h"
#include "timer1.h"

typedef enum {
    STATE_IDLE,         // Čekání na preambuli v šumu
    STATE_SYNC_WAIT,    // Preambule nalezena, čekáme na první Sync Word
    STATE_RECEIVING,    // Pevné časování, příjem datových slov
	STATE_TRANSMITING   // Během vysílání se musí blokovat příjem.
} POCSAG_State;

static volatile POCSAG_State state = STATE_IDLE;
static volatile uint32_t shiftReg = 0;
static volatile uint16_t bitCounter = 0;
volatile POCSAG_Message currentMsg;
static uint32_t bitBuffer = 0;
static uint8_t bitsInBuffer = 0;


// --- BCH (31,21) a Parita ---
// Pomocná funkce pro zrcadlení bitů v 32-bitovém slově
__attribute__((unused)) static uint32_t reverse32(uint32_t x) {
    x = ((x >> 1) & 0x55555555) | ((x & 0x55555555) << 1);
    x = ((x >> 2) & 0x33333333) | ((x & 0x33333333) << 2);
    x = ((x >> 4) & 0x0F0F0F0F) | ((x & 0x0F0F0F0F) << 4);
    x = ((x >> 8) & 0x00FF00FF) | ((x & 0x00FF00FF) << 8);
    return (x >> 16) | (x << 16);
}

static uint32_t calculate_syndrom(uint32_t word) {
    // POCSAG používá pro výpočet syndromu bity v pořadí, jak přicházely.
    // Musíme pracovat s bity od MSB k LSB (bit 31 je x^30).

    // Vytvoříme pracovní kopii a vynulujeme paritu (bit 0)
    uint32_t reg = word & 0xFFFFFFFE;

    // Polynom: x^10 + x^9 + x^8 + x^6 + x^5 + x^3 + 1  => 0b11101101001 => 0x769
    // Pro dělení shora (od bitu 31) musíme polynom zarovnat doleva:
    // 0x769 zarovnaný tak, aby x^10 odpovídalo bitu 31:
    uint32_t poly = 0xED200000; // To je 0x769 posunutý tak, aby začínal na bitu 31

    for (int i = 0; i < 21; i++) {
        if (reg & (1UL << (31 - i))) {
            reg ^= (poly >> i);
        }
    }
    // Syndrom jsou bity 10 až 1. Pokud je slovo OK, musí být tyto bity v reg nulové.
    return (reg & 0x000007FE);
}

static bool check_parity(uint32_t word) {
    // Celkové slovo (32 bitů) má mít SUDOU paritu
    uint32_t p = 0;
    for (int i = 0; i < 32; i++) {
        if (word & (1UL << i)) p++;
    }
    return (p % 2 == 0);
}
static uint32_t try_fix_word(uint32_t word, bool *fixed) {
    *fixed = false;

    // 1. Krok: Je slovo už v pořádku?
    if (calculate_syndrom(word) == 0 && check_parity(word)) {
        return word;
    }

    // 2. Krok: Pokus o opravu jednoho bitu (brute force přes všech 32 pozic)
    for (int i = 0; i < 32; i++) {
        uint32_t test_word = word ^ (1UL << i);
        if (calculate_syndrom(test_word) == 0 && check_parity(test_word)) {
            *fixed = true;
            return test_word;
        }
    }

    return word; // Neopravitelné (více chyb)
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
//    if (bit==1) {GPIO_PinOutSet(PTT_PORT, PTT_PIN);}
//    else {GPIO_PinOutClear(PTT_PORT, PTT_PIN);}

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
			case STATE_TRANSMITING:
				bitCounter--;
				if (bitCounter == 0) {
					//-- Konec vysilani
					GPIO_PinOutSet(PTT_PORT, PTT_PIN);
/*
					GPIO_IntEnable(1 << RX_PIN);
					LED4_Off();
					state = STATE_IDLE;
					TIMER1_Stop();
*/
					POCSAG_Init();
				}
				else {
					//-- Vysila
					GPIO_PinOutToggle(TX_PORT, TX_PIN);
				}
				break;
    }
}

// Pomocná funkce pro dekódování 7-bit ASCII (upraveno pro korektní bit-order)
void decode_ascii_part(uint32_t word, char *outStr) {
    uint32_t data = (word >> 11) & 0xFFFFF; // 20 bitů dat

    for (int i = 0; i < 20; i++) {
        bitBuffer <<= 1;
        if (data & (1UL << (19 - i))) bitBuffer |= 1;
        bitsInBuffer++;

        if (bitsInBuffer == 7) {
            // POCSAG ASCII je LSB-first
            uint8_t c = 0;
            for(int b=0; b<7; b++) {
                if(bitBuffer & (1 << b)) c |= (1 << (6-b));
            }

            if (c >= 32 && c <= 126) {
                size_t len = strlen(outStr);
                if (len < 120) {
                    outStr[len] = (char)c;
                    outStr[len+1] = '\0';
                }
            }
            bitBuffer = 0;
            bitsInBuffer = 0;
        }
    }
}

//------------------------------------------------------------------------------
//  Vysilani datagramu
//------------------------------------------------------------------------------
void POCSAG_Tx_datagram(void) {
	// Zastavit a zablokovat Rx
	TIMER1_Stop();
    GPIO_IntDisable(1 << RX_PIN); // VYPNEME HRANY - teď už jen pevný čas
	state = STATE_TRANSMITING;

	// Spusti vysilani
	bitCounter=5000;
	GPIO_PinOutClear(PTT_PORT, PTT_PIN);  // zaklicuje
	TIMER1_Start();
	LED4_On();

}

//------------------------------------------------------------------------------
//  Zpracovani prijateho datagramu
//------------------------------------------------------------------------------
void POCSAG_Process(void) {
    if (!currentMsg.ready) return;

    char buf[160];
    char textMsg[128] = {0};
    bitBuffer = 0;
    bitsInBuffer = 0;

    sendStringUART1("\r\n--- POCSAG DATAGRAM START ---\r\n");

    //--- Výpis surových dat a kontrola/oprava CDW
    for (uint16_t i = 0; i < currentMsg.total_words; i++) {
        uint32_t raw = currentMsg.data[i];

        if (raw == POCSAG_IDLE_WORD) {
            sprintf(buf, "W[%03d]: IDLE\r\n", i);
            sendStringUART1(buf);
            continue;
        }

        bool fixed = false;
        uint32_t clean = try_fix_word(raw, &fixed);
        bool valid = (calculate_syndrom(clean) == 0 && check_parity(clean));

        sprintf(buf, "W[%03d]: %08X %s %s\r\n",
                i, (unsigned int)raw, valid ? "OK " : "ERR", fixed ? "[FIXED]" : "");
        sendStringUART1(buf);
    }

	//---------------------- Nacte udaje z hlavicky
    currentMsg.Ttoken= (currentMsg.data[2]>>16)&0x1F;
    currentMsg.Tbatch= (currentMsg.data[1]>>25)&0x3F;
    currentMsg.Tnet =  (currentMsg.data[0]>>24)&0x0F;
    currentMsg.Tadr =  (currentMsg.data[0]>>16)&0x1F;
    currentMsg.Tdau =  (currentMsg.data[2]>>26)&0x1F;
    currentMsg.Tpath = (currentMsg.data[0]>>12)&0x0F;
    currentMsg.Tmaster=(currentMsg.data[1]>>16)&0x1F;
    currentMsg.Tsystem=0x01==((currentMsg.data[0]>>21)&0x07);

    //--- Vypise hlavicku
//    sendStringUART1("\r\n--- HEADER ---\r\n");

	if(currentMsg.Tsystem==1) {
		sendStringUART1("\r\n--- HEADER - SYSTEM TOKEN ---\r\n");
	}
	else {
		sendStringUART1("\r\n--- HEADER - NORMAL TOKEN ---\r\n");
	}
	sprintf(buf,"NET=%02u DAU=%02u ADR=%u PATH=%u\r\n",currentMsg.Tnet,currentMsg.Tdau,currentMsg.Tadr,currentMsg.Tpath);
    sendStringUART1(buf);
	sprintf(buf,"TOKEN=%02u BATCH=%02u MASTER=%u\r\n",currentMsg.Ttoken,currentMsg.Tbatch,currentMsg.Tmaster);
    sendStringUART1(buf);

    //--- Dekódování adresy a textu --- az od ctvrteho codewordu, za hlavickou
    if(currentMsg.Tsystem==0) {
		sendStringUART1("\r\n--- MESSAGES ---\r\n");

		for (uint16_t i = 3; i < currentMsg.total_words; i++) {
			uint32_t raw = currentMsg.data[i];
			if (raw == POCSAG_IDLE_WORD) continue;

			bool fixed = false;
			uint32_t clean = try_fix_word(raw, &fixed);
			if (calculate_syndrom(clean) != 0 || !check_parity(clean)) continue;

			if ((clean & 0x80000000) == 0) {
				// Výpočet úplné RIC adresy (Adresa + Frame Index)
				uint8_t wordInBatchPos = i % 16;
				uint8_t frameIndex = wordInBatchPos / 2;
				uint32_t addrPart = (clean >> 13) & 0x3FFFF;
				uint32_t fullRIC = (addrPart << 3) | (frameIndex & 0x07);
				uint8_t func = (clean >> 11) & 0x03;

				sprintf(buf, "ADRESA: %07lu (Funkce %d)\r\n", (unsigned long)fullRIC, func);
				sendStringUART1(buf);

				textMsg[0] = '\0';
				bitBuffer = 0;
				bitsInBuffer = 0;
			}
			else {
				decode_ascii_part(clean, textMsg);
			}
		}
    }

    if (textMsg[0] != '\0') {
        sendStringUART1("TEXT  : ");
        sendStringUART1(textMsg);
        sendStringUART1("\r\n");
    }

    sendStringUART1("--- DATAGRAM END ---\r\n\r\n");
    currentMsg.ready = false;
}
