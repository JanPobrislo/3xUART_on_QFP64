#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "pocsag.h"
#include "parameters.h"
#include "inputs.h"
#include "uart1.h"
#include "em_timer.h"
#include "em_gpio.h"
#include "ports.h"
#include "led.h"
#include "timer1.h"
#include "rtc.h"

typedef enum {
    STATE_RX_IDLE,      // Čekání na preamble v šumu
	STATE_PREAMBLE,		// Preamble nalezen, zacne kalibrovat
//	STATE_PREAMBLE_CALIBRATED,	// Cte az do konce preamble
    STATE_SYNC_WORD,    // Konec preamble, čeká na první Sync Word
    STATE_RECEIVING,    // Pevné časování, příjem datových slov
	STATE_TRANSMITING   // Během vysílání se musí blokovat příjem.
} POCSAG_Rx_State;

static volatile POCSAG_Rx_State rx_state = STATE_RX_IDLE;
static volatile uint32_t shiftReg = 0;
static volatile uint16_t bitCounter = 0;
POCSAG_token rx_token;
static uint32_t bitBuffer = 0;
static uint8_t bitsInBuffer = 0;

typedef enum {
    STATE_TX_IDLE, 	// Nic nedela, ceka az bude vysilat
    TX_PREAMBLE, 	// Vysila preamble
    TX_SYNC,  		// Vysila FS - synchronizacni slovo
    TX_CDW,  		// Vysila codeword - datove slovo
} POCSAG_Tx_State;
static POCSAG_Tx_State tx_state = STATE_TX_IDLE;
static volatile uint16_t number_of_tx = 0;    // Pocet vyslanych bitu
static volatile uint16_t number_of_words = 0; // Pocet vyslanych slov CDW

typedef enum {
    STATE_ROUTE_IDLE,  // Nic nedela, ceka az bude vysilat
    WAIT_FOLLOW,  	// Ceka na vysilac v prime ceste
    WAIT_ERROR,  	// Ceka na vysilac v chybove ceste
//    WAIT_REVERS,  	// Ceka na vysilac v reverzni ceste
} POCSAG_Route_State;

static POCSAG_Route_State route_state = STATE_ROUTE_IDLE;
static uint32_t route_repeat_counter = 0;
static uint32_t route_timer = 0;

POCSAG_token tx_token;

// --- BCH (31,21) a Parita ---
// Pomocná funkce pro zrcadlení bitů v 32-bitovém slově
__attribute__((unused)) static uint32_t reverse32(uint32_t x) {
    x = ((x >> 1) & 0x55555555) | ((x & 0x55555555) << 1);
    x = ((x >> 2) & 0x33333333) | ((x & 0x33333333) << 2);
    x = ((x >> 4) & 0x0F0F0F0F) | ((x & 0x0F0F0F0F) << 4);
    x = ((x >> 8) & 0x00FF00FF) | ((x & 0x00FF00FF) << 8);
    return (x >> 16) | (x << 16);
}

//------------------------------------------------------------------------------
// Vrací jeden bit z 32bitov0ho slova. 1=nejnizsi bit .. 32=nejvyzsi bit.
//------------------------------------------------------------------------------
uint8_t get_bit(uint32_t word, uint8_t bit_pos) {
    // Kontrola rozsahu (povolené hodnoty 1 až 32)
    if (bit_pos < 1 || bit_pos > 32) {
        return 0;
    }

    // Výpočet posunu:
    // Pro bit_pos = 32 chceme posun o 31 (nejvyšší bit)
    // Pro bit_pos = 1 chceme posun o 0 (nejnižší bit)
    uint8_t shift = bit_pos - 1;

    // Izolace bitu pomocí masky a posunu
    if (word & (1UL << shift)) {
        return 1;
    } else {
        return 0;
    }
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

//------------------------------------------------------------------------------
//  Vypocet BCH a PARITY
//------------------------------------------------------------------------------
/**
 * @brief Vypočítá BCH(31,21) a paritu pro všechna slova v tx_token.
 * Předpokládá, že v tx_token.data[i] je uloženo 21 informačních bitů
 * zarovnaných doleva (bity 31 až 11).
 * @brief Vypočítá BCH a paritu dle POCSAG standardu (včetně inverze kontrolních bitů).
 * Generuje správné IDLE slovo 0x7A89C197 z dat 0x7A89C...
 */
void make_bch(POCSAG_token *token) {
    if (token == NULL) return;

    /*
     * POCSAG BCH(31,21) + sudá parita
     * ---------------------------------
     * Každé 32-bitové slovo má strukturu:
     *   bit 31..11 = 21 datových bitů (MSB první)
     *   bit 10..1  = 10 BCH kontrolních bitů
     *   bit 0      = paritní bit (sudá parita celého slova)
     *
     * Generátor: g(x) = x^10 + x^9 + x^8 + x^6 + x^5 + x^3 + 1 = 0x769
     *
     * Algoritmus:
     *   1. Vezmi 21 datových bitů (bity 31..11 vstupního slova).
     *   2. Sestav 31-bit codeword základ: data21 << 10  (bity 30..10).
     *   3. Polynomiální dělení GF(2): odečítej generátor od MSB dolů.
     *      Zbytek (bity 9..0) jsou BCH kontrolní bity.
     *   4. Sestav výsledné 32-bit slovo: (data21 << 11) | (bch << 1).
     *   5. Nastav bit 0 (paritu) tak aby byl celkový počet jedniček sudý.
     *
     * Chyby původní verze:
     *   - Použití poly=0x1B5 (chybný generátor; správný je 0x769).
     *   - Inverze reg ^= 0x3FF (POCSAG BCH inverzi NEPOŽADUJE).
     *   - Počítání parity jen přes 31 bitů místo 32.
     */

    const uint32_t GEN = 0x769u;  /* generátor BCH(31,21) */

    for (int i = 0; i < token->total_words; i++) {
        uint32_t in = token->data[i];

        /* IDLE slovo necháme beze změny */
        if (in == POCSAG_IDLE_WORD) continue;

        /* 1. Extrahuj 21 datových bitů */
        uint32_t data21 = (in >> 11) & 0x1FFFFFu;

        /* 2. Základ 31-bit codeword: data na pozicích 30..10 */
        uint32_t cw = data21 << 10;

        /* 3. Polynomiální dělení – odečítáme generátor od MSB dolů */
        for (int b = 20; b >= 0; b--) {
            if (cw & (1u << (b + 10))) {
                cw ^= (GEN << b);
            }
        }
        /* cw[9..0] = BCH zbytek (kontrolní bity) */

        /* 4. Sestav 32-bit slovo: data[31..11] | bch[10..1] | parita[0] */
        uint32_t word = (data21 << 11) | ((cw & 0x3FFu) << 1);

        /* 5. Sudá parita přes všech 32 bitů */
        uint32_t tmp = word;
        uint8_t  par = 0;
        while (tmp) { par ^= (uint8_t)(tmp & 1u); tmp >>= 1; }
        if (par) word |= 1u;  /* nastav bit 0 aby byl počet jedniček sudý */

        token->data[i] = word;
    }
}

//------------------------------------------------------------------------------
// Init prijmu
//------------------------------------------------------------------------------
void POCSAG_rx_init(void) {
    rx_state = STATE_RX_IDLE;
    rx_token.ready = false;
    rx_token.total_words = 0;
	tx_state = STATE_TX_IDLE;
	shiftReg = 0;

	calib_start = false;
	calib_stop = false;
//	calib_start_counter = 0;
//	calib_stop_counter = 0;

    // Povolení přerušení od hran PA0
    GPIO_ExtIntConfig(RX_PORT, RX_PIN, RX_PIN, true, true, true);
    GPIO_IntClear(1 << RX_PIN);   // vymazat pending flag PŘED povolením
    rx_edge_irq_enabled();

    // Timer1 na vychozi hodnoty
    initTIMER1();
    TIMER1_Start();  // citac pobezi trvale
}

//------------------------------------------------------------------------------
// Synchro na hranu signalu - Voláno z GPIO_EVEN_IRQHandler
//------------------------------------------------------------------------------
void POCSAG_edge_detected(void) {
//    if (rx_state != STATE_RECEIVING)
//    if (rx_state == STATE_RX_IDLE)
    {
		// Resetujeme časovač na polovinu periody, aby první Sample přišel do středu bitu
//        TIMER1->CNT = TIMER1_TOP / 2;
        TIMER1->CNT = TIMER1->TOP / 2;    // v TOP je kalibrovana hodnota
    }
}

//------------------------------------------------------------------------------
//  Ukonceni vysilani datagramu
//------------------------------------------------------------------------------
void tx_stop(void) {
	tx_state = STATE_TX_IDLE;
	GPIO_PinOutSet(PTT_PORT, PTT_PIN);  	// odklicuje
	LED2_Off();
	LED3_Off();
	POCSAG_rx_init();  // inicializuje prijem
	rx_state = STATE_RX_IDLE;
}

//------------------------------------------------------------------------------
// Nastavi TX BIT
//------------------------------------------------------------------------------
void set_tx_bit(uint8_t bit) {
	if (bit==1) {
		GPIO_PinOutSet(TX_PORT, TX_PIN);
		sendStringUART1("1");
	}
	else {
		GPIO_PinOutClear(TX_PORT, TX_PIN);
		sendStringUART1("0");
	}
}

//------------------------------------------------------------------------------
// Vysila BIT - Voláno z sample_bit() spoustenym z TIMER1_IRQHandler (1200 Hz)
//------------------------------------------------------------------------------
void tx_bit(void) {
    switch (tx_state) {
        case STATE_TX_IDLE:
        	break;
        case TX_PREAMBLE:
			//-- Vysila
			number_of_tx++;
			GPIO_PinOutToggle(TX_PORT, TX_PIN);
			if (number_of_tx == 576) {  //-- preamble ma 576 bitu
				number_of_tx = 0;
				number_of_words = 0;
				tx_state = TX_SYNC;
				sendStringUART1("\n");
			}
        	break;
        case TX_SYNC:
        	number_of_tx++;
        	set_tx_bit(get_bit(POCSAG_SYNC_WORD, 33-number_of_tx));  //-- Nastavi TX BIT
			if (number_of_tx == 32) {  //-- 32 bitu sync word
				number_of_tx = 0;
				tx_state = TX_CDW;
				sendStringUART1("\n");
			}
        	break;
        case TX_CDW:
        	number_of_tx++;
        	set_tx_bit(get_bit(tx_token.data[number_of_words], 33-number_of_tx));  //-- Nastavi TX BIT
			if (number_of_tx == 32) {  //-- 32 bitu = vyslano cele slovo
				number_of_tx = 0;
				number_of_words++;
				sendStringUART1("\n");
				if(number_of_words >= tx_token.total_words) {  //-- vyslan cely token
					tx_stop();
					sendStringUART1("--------------------------------\n");
					sendStringUART1("TxEND\n");
				}
				else {
					if(number_of_words%16 == 0) {  //-- konec batch nasleduje SYNC WORD
						number_of_tx = 0;
						tx_state = TX_SYNC;
						sendStringUART1("\n");
					}
				}
			}
        	break;
    }
}

//------------------------------------------------------------------------------
// Odecte nebo vysila BIT - Voláno z TIMER1_IRQHandler (1200 Hz)
//------------------------------------------------------------------------------
void POCSAG_sample_bit(void) {
//    char buf[160];
    static uint8_t wordsInBatch = 0; // Sleduje pozici v rámci aktuálního batche (0-15)

	uint8_t bit = (Input_GetRX() > 0) ? 1 : 0;
    shiftReg = (shiftReg << 1) | bit;

//    GPIO_PinOutToggle(DBG_PORT, DBG_PIN);
    //LED2_Toggle();

    switch (rx_state) {
        case STATE_RX_IDLE:
            LED1_Toggle();
			// Hledáme střídavou sekvenci (01010101...) v posledních 16 bitech
			// 0xAAAA je 1010101010101010, 0x5555 je 0101010101010101
			if ((shiftReg == 0xAAAAAAAA) || (shiftReg == 0x55555555)) {
//			if ((uint16_t)(shiftReg & 0xFFFF) == 0xAAAA || (uint16_t)(shiftReg & 0xFFFF) == 0x5555) {
                rx_state = STATE_PREAMBLE;
                TIMER1_ResetSpeed();
                bitCounter = 0;
//            	calib_start_counter = 0;
//            	calib_stop_counter = 0;
                TIMER_CounterSet((TIMER_TypeDef *)WTIMER0, 0); // Nuluje counter
            	calib_bits = 0;
        		calib_start = true;
            	LED1_On();
            }
            break;

        case STATE_PREAMBLE:
			bitCounter++;
			// Ceka do konce preamble
			if ((shiftReg != 0xAAAAAAAA) && (shiftReg != 0x55555555)) {
				//-- Zkusi nacist FS (sync.word)
                rx_state = STATE_SYNC_WORD;
//                calib_bits = bitCounter;  // predelano do IRQ od RX (PA0)
        		calib_stop = true;
                bitCounter = 0;
            }
            break;


        case STATE_SYNC_WORD:
            if (shiftReg == POCSAG_SYNC_WORD) {
                rx_state = STATE_RECEIVING;
                bitCounter = 0;
                wordsInBatch = 1; // Dalších 16 slov jsou data
                rx_token.total_words = 0;
            	rx_edge_irq_disabled(); // Vypneme detekci hran - teď už jen pevný čas
            }
            else {
                bitCounter++;
                if (bitCounter >= 32) {
                	//-- FS nenalezen
                	TIMER1_ResetSpeed();
					rx_edge_irq_enabled();
					rx_state = STATE_RX_IDLE;
                }
            }
            break;

		case STATE_RECEIVING:
			bitCounter++;
			//-- Synchronizace na hranu mezi prvnimi dva bity FS (01111100110100100001010111011000)
			if (wordsInBatch == 0 && bitCounter == 1) {
            	rx_edge_irq_enabled(); // Zapneme detekci hran - synchr
                TIMER1->CNT  = 0; // Nuluje timer aby IRQ od hrany mel sanci
			}
			if (wordsInBatch == 0 && bitCounter == 2) {
            	rx_edge_irq_disabled(); // Vypneme detekci hran - teď už jen pevný čas
			}

			//-- Právě jsme dočetli 32. bit. Obsah je v shiftReg.
			if (bitCounter >= 32) {
				bitCounter = 0;

				// SCÉNÁŘ A: Čekáme na SYNC slovo (každých 17. slovo v proudu dat)
				if (wordsInBatch == 0) {
					if (shiftReg == POCSAG_SYNC_WORD) {
						// V pořádku, začíná další batch
						// wordsInBatch necháme na 0, ale nepíšeme SYNC do dat
					} else {
						// KONEC DATAGRAMU: Na místě, kde měl být SYNC, je něco jiného
						rx_token.ready = true;
						rx_state = STATE_RX_IDLE;
//						TIMER1->CMD = TIMER_CMD_STOP;

//						sprintf(buf,"\r\nTIMER1.TOP=%lu",TIMER1->TOP);
//					    sendStringUART1(buf);

						TIMER1_ResetSpeed();
    					rx_edge_irq_enabled();
						return;
					}
				}

				// SCÉNÁŘ B: Čteme datové slovo (1-16)
				else {
					if (rx_token.total_words < (MAX_BATCHES * WORDS_PER_BATCH)) {
						rx_token.data[rx_token.total_words++] = shiftReg;
					}
				}

				// Inkrementace a reset čítače batche (0-16, kde 0 je pozice pro SYNC)
				wordsInBatch++;
				if (wordsInBatch > 16) {
					wordsInBatch = 0; // Příští slovo MUSÍ být SYNC
				}

				// Ochrana proti přetečení celkového pole
				if (rx_token.total_words >= (MAX_BATCHES * WORDS_PER_BATCH)) {
					rx_token.ready = true;
					rx_state = STATE_RX_IDLE;
//					TIMER1->CMD = TIMER_CMD_STOP;
					TIMER1_ResetSpeed();
					rx_edge_irq_enabled();
				}
			}
			break;

		case STATE_TRANSMITING:
			tx_bit();
			break;
    }
}

//------------------------------------------------------------------------------
// Vypise stav rx_state na UART1 (COM-B)
//------------------------------------------------------------------------------
void POCSAG_show_rx_state(void) {
	sendStringUART1(" RX STATE: ");
    switch (rx_state) {
        case STATE_RX_IDLE:
			sendStringUART1("STATE_RX_IDLE");
            break;

        case STATE_PREAMBLE:
			sendStringUART1("STATE_PREAMBLE");
            break;
/*
        case STATE_PREAMBLE_CALIBRATED:
			sendStringUART1("STATE_PREAMBLE_CALIBRATED");
            break;
*/
        case STATE_SYNC_WORD:
			sendStringUART1("STATE_SYNC_WAIT");
            break;

		case STATE_RECEIVING:
			sendStringUART1("STATE_RECEIVING");
			break;

		case STATE_TRANSMITING:
			sendStringUART1("STATE_TRANSMITING");
			break;

		default:
			sendStringUART1("UNKNOWN");
			break;
    }
	sendStringUART1("\r\n");
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
//  Z binární hlavičky v poli data[] nastavi všechny promnene ve struktuře
//------------------------------------------------------------------------------
void read_header(POCSAG_token *token) {
    if (token == NULL) return;

    token->token_id = (token->data[2]>>16)&0x1F;
    token->batch= (token->data[1]>>25)&0x3F;
    token->net =  (token->data[0]>>24)&0x0F;
    token->adr =  (token->data[0]>>16)&0x1F;
    token->dau =  (token->data[2]>>26)&0x1F;
    token->path = (token->data[0]>>12)&0x0F;
    token->master =(token->data[1]>>16)&0x1F;
    token->token_type = (token->data[0]>>21)&0x07;

    token->distribution = (token->data[0]>>28)&0x03;
    token->pass_dau = (token->data[2]>>21)&0x1F;
    token->alarm_dau = (token->data[1]>>11)&0x1F;
    token->alarm_no = (token->data[1]>>21)&0x0F;
}

//------------------------------------------------------------------------------
//  Nastavi binární hlavičku v poli data[] na základě všech promnenych ve struktuře
//------------------------------------------------------------------------------
/*
void make_header(POCSAG_token *token) {
    if (token == NULL) return;

    uint32_t d0 = token->data[0];           // zachovat původní obsah
    d0 &= ~(0x0FUL << 24);                  // vymazat bity 27..24
    d0 &= ~(0x01UL << 21);                  // vymazat bit  21
    d0 &= ~(0x1FUL << 16);                  // vymazat bity 20..16
    d0 &= ~(0x0FUL << 12);                  // vymazat bity 15..12
    d0 |= ((uint32_t)(token->net          & 0x0F) << 24);
    d0 |= ((uint32_t)(token->token_type   & 0x07) << 21);
    d0 |= ((uint32_t)(token->adr          & 0x1F) << 16);
    d0 |= ((uint32_t)(token->path         & 0x0F) << 12);
    token->data[0] = d0;

    uint32_t d1 = token->data[1];           // zachovat původní obsah
    d1 &= ~(0x3FUL << 25);                  // vymazat bity 30..25
    d1 &= ~(0x1FUL << 16);                  // vymazat bity 20..16
    d1 |= ((uint32_t)(token->batch        & 0x3F) << 25);
    d1 |= ((uint32_t)(token->master       & 0x1F) << 16);
    token->data[1] = d1;

    uint32_t d2 = token->data[2];           // zachovat původní obsah
    d2 &= ~(0x1FUL << 26);                  // vymazat bity 30..26
    d2 &= ~(0x1FUL << 16);                  // vymazat bity 20..16
    d2 |= ((uint32_t)(token->dau          & 0x1F) << 26);
    d2 |= ((uint32_t)(token->token_id     & 0x1F) << 16);
    token->data[2] = d2;

	make_bch(&tx_token);     //-- Opravi BCH a Paritu
}
*/

void make_header(POCSAG_token *token) {
    if (token == NULL) return;

    /* --- data[0] --- */
    uint32_t d0 = token->data[0];
    d0 &= ~(0x03UL << 28);                  // vymazat bity 29..28  (distribution)
    d0 &= ~(0x0FUL << 24);                  // vymazat bity 27..24  (net)
    d0 &= ~(0x07UL << 21);                  // vymazat bity 23..21  (token_type)
    d0 &= ~(0x1FUL << 16);                  // vymazat bity 20..16  (adr)
    d0 &= ~(0x0FUL << 12);                  // vymazat bity 15..12  (path)
    d0 |= ((uint32_t)(token->distribution & 0x03) << 28);
    d0 |= ((uint32_t)(token->net          & 0x0F) << 24);
    d0 |= ((uint32_t)(token->token_type   & 0x07) << 21);
    d0 |= ((uint32_t)(token->adr          & 0x1F) << 16);
    d0 |= ((uint32_t)(token->path         & 0x0F) << 12);
    token->data[0] = d0;

    /* --- data[1] --- */
    uint32_t d1 = token->data[1];
    d1 &= ~(0x3FUL << 25);                  // vymazat bity 30..25  (batch)
    d1 &= ~(0x0FUL << 21);                  // vymazat bity 24..21  (alarm_no)
    d1 &= ~(0x1FUL << 16);                  // vymazat bity 20..16  (master)
    d1 &= ~(0x1FUL << 11);                  // vymazat bity 15..11  (alarm_dau)
    d1 |= ((uint32_t)(token->batch        & 0x3F) << 25);
    d1 |= ((uint32_t)(token->alarm_no     & 0x0F) << 21);
    d1 |= ((uint32_t)(token->master       & 0x1F) << 16);
    d1 |= ((uint32_t)(token->alarm_dau    & 0x1F) << 11);
    token->data[1] = d1;

    /* --- data[2] --- */
    uint32_t d2 = token->data[2];
    d2 &= ~(0x1FUL << 26);                  // vymazat bity 30..26  (dau)
    d2 &= ~(0x1FUL << 21);                  // vymazat bity 25..21  (pass_dau)
    d2 &= ~(0x1FUL << 16);                  // vymazat bity 20..16  (token_id)
    d2 |= ((uint32_t)(token->dau          & 0x1F) << 26);
    d2 |= ((uint32_t)(token->pass_dau     & 0x1F) << 21);
    d2 |= ((uint32_t)(token->token_id     & 0x1F) << 16);
    token->data[2] = d2;

//    make_bch(token);  //udela tesne pred vysilanim
}

//------------------------------------------------------------------------------
//  Zapise status tohoto DAU do systemoveho tokenu a inc TxCOUNTER
//------------------------------------------------------------------------------
void make_system_status(POCSAG_token *token) {
    if (token == NULL) return;

    unsigned char cdw,pos,status,counter;

    cdw = ((token->dau-1)/4)+3;				//-- v kterem CDW je status
    pos = ((3-((token->dau-1)%4)) * 5)+11;  //-- na jake pozici

    status = token->tx_status;
    if (Input_GetOnBattery()==1) {status += 16; }
    if (Input_GetTamper()==1) {status += 8; }

    //-- zapise status DAU
    token->data[cdw] &= ~(0x1FUL << pos);   // vymazat 5 bitu status dau
    token->data[cdw] |= ((uint32_t)(status & 0x1F) << pos);

    //-- inkrementuje TxCounter
    counter =(token->data[11]>>23)&0xFF;
    counter++;
    token->data[11] &= ~(0xFFUL << 23);   // vymazat 8 bitu
    token->data[11] |= ((uint32_t)counter << 23);

//    make_bch(token);  //udela tesne pred vysilanim

    char buf[160];
    sprintf(buf, "\r\nSTATUS DAU:0x%X on CDW=%u POS=%u\r\n ",status,cdw,pos);
	sendStringUART1(buf);
}

//------------------------------------------------------------------------------
//  Vysilani datagramu
//------------------------------------------------------------------------------
/*
void POCSAG_Tx_datagram(void) {
	// Zastavit a zablokovat Rx
	TIMER1_Stop();
    GPIO_IntDisable(1 << RX_PIN); // VYPNEME HRANY - teď už jen pevný čas
	rx_state = STATE_TRANSMITING;

	// Spusti vysilani
	bitCounter=5000;
	GPIO_PinOutClear(PTT_PORT, PTT_PIN);  // zaklicuje
	TIMER1_Start();
	LED4_On();

}
*/

//------------------------------------------------------------------------------
//  Spusteni vysilani datagramu
//------------------------------------------------------------------------------
void tx_start(void) {
    char buf[160];

	LED2_On();

	//-- Zastavit a zablokovat Rx
	TIMER1_Stop();
	rx_edge_irq_disabled(); // Vypneme detekci hran - nevyhodnocuje prijem
//    GPIO_IntDisable(1 << RX_PIN); // VYPNEME HRANY - nevyhodnocuje prijem
	rx_state = STATE_TRANSMITING;

	//-- Zkontroluje zda muze a ma vysilat alarm
	if (tx_token.alarm_dau == 0) {
		//-- je misto muze zapsat alarm

		if (tamper_alarm_status() == ALARM_START) {
			//-- vyhlasi tamper alarm  - start
			tx_token.alarm_dau = tx_token.dau;
			tx_token.alarm_no = ERROR_NO_TAMPER_START;
			set_tamper_alarm(ALARM_SENT);
			sendStringUART1("TX ALARM: ERROR_NO_TAMPER_START\r\n");
		}

		if (tamper_alarm_status() == ALARM_END) {
			//-- vyhlasi tamper alarm  - konec
			tx_token.alarm_dau = tx_token.dau;
			tx_token.alarm_no = ERROR_NO_TAMPER_END;
			set_tamper_alarm(NORMAL_CONDITION);
			sendStringUART1("TX ALARM: ERROR_NO_TAMPER_END\r\n");
		}

		if (batery_alarm_status() == ALARM_START) {
			//-- vyhlasi batery alarm  - start
			tx_token.alarm_dau = tx_token.dau;
			tx_token.alarm_no = ERROR_NO_BATERY_START;
			set_batery_alarm(ALARM_SENT);
			sendStringUART1("TX ALARM: ERROR_NO_BATERY_START\r\n");
		}

		if (batery_alarm_status() == ALARM_END) {
			//-- vyhlasi batery alarm  - konec
			tx_token.alarm_dau = tx_token.dau;
			tx_token.alarm_no = ERROR_NO_BATERY_END;
			set_batery_alarm(NORMAL_CONDITION);
			sendStringUART1("TX ALARM: ERROR_NO_BATERY_END\r\n");
		}
	}

	//-- Vygeneruje binární podobu hlavičky
	make_header(&tx_token);

	//-- Do systemoveho tokenu zapise svuj STATUS DAU
	if (tx_token.token_type == SYSTEM_TOKEN) {
		make_system_status(&tx_token);
	}

	//-- Opravi BCH+PARITU celeho tokenu
	make_bch(&tx_token);

	sendStringUART1("\r\n-------------- TX --------------\r\n");
	sendStringUART1("TX: ");
    //--- Vypise TX hlavicku
    switch (tx_token.token_type) {
		case SYSTEM_TOKEN:
			sendStringUART1("SYSTEM ");
			break;
		case NORMAL_TOKEN:
			sendStringUART1("NORMAL ");
			break;
		case TEST_TOKEN:
			sendStringUART1("PP-TEST ");
			break;
	}
	sprintf(buf,"NET=%02u DAU=%02u ADR=%02u PATH=%u ",tx_token.net,tx_token.dau,tx_token.adr,tx_token.path);
    sendStringUART1(buf);
	sprintf(buf,"TOKEN=%u BATCH=%u MASTER=%02u\r\n",tx_token.token_id,tx_token.batch,tx_token.master);
    sendStringUART1(buf);
	sprintf(buf,"ROUTE: FOLLOW=%02u ERROR=%02u REVERSAL=%02u",tx_route.follow, tx_route.error, tx_route.revers);
    sendStringUART1(buf);

    sendStringUART1("  DISTRIBUTION: ");
    switch (rx_token.distribution) {
		case DIRECT_TOKEN:
			sendStringUART1("DIRECT\r\n");
			break;
		case REPAIR_TOKEN:
			sendStringUART1("REPAIR\r\n");
			break;
		case REVERSE_TOKEN:
			sendStringUART1("REVERSE\r\n");
			break;
	}

	//-- Spusti vysilani
	tx_state = TX_PREAMBLE;
	number_of_tx = 0;
	GPIO_PinOutClear(TX_PORT, TX_PIN);    	// nula aby preamble zacal 1
	GPIO_PinOutClear(PTT_PORT, PTT_PIN);  	// zaklicuje
	TIMER1_ResetSpeed();
	TIMER1_Start();
}

//------------------------------------------------------------------------------
//  Zpracovani prijateho datagramu
//------------------------------------------------------------------------------
void POCSAG_process(void) {
    if (!rx_token.ready) return;

    char buf[160];
    char textMsg[128] = {0};
    bitBuffer = 0;
    bitsInBuffer = 0;

    sendStringUART1("\r\n--- RX START ---\r\n");

    rx_token.rx_ok = true; // Neopravena chyba to pripadne schodi

    //--- Výpis surových dat a kontrola/oprava CDW
    for (uint16_t i = 0; i < rx_token.total_words; i++) {
        uint32_t raw = rx_token.data[i];

        if (raw == POCSAG_IDLE_WORD) {
            sprintf(buf, "%02d: IDLE\r\n", i+1);
            sendStringUART1(buf);
            continue;
        }

        bool fixed = false;
        uint32_t clean = try_fix_word(raw, &fixed);
        bool valid = (calculate_syndrom(clean) == 0 && check_parity(clean));

        if (valid) {
            rx_token.data[i] = clean;  // uložit opravenou hodnotu zpět
        }
        else {
        	rx_token.rx_ok = false;
        }

        sprintf(buf, "%02d: %08X %s %s\r\n",
                i+1, (unsigned int)rx_token.data[i],
				valid ? "OK " : "ERR", fixed ? "[FIXED]" : "");
        sendStringUART1(buf);

        //-- Pro hlavicku staci OK prvni 3 CDW
        if (i == 2) {
        	rx_token.header_ok = rx_token.rx_ok;
        }
    }

//    sendStringUART1("-----------------------------\r\n");

    //---------------------- Nacte udaje z hlavicky
    read_header(&rx_token);

    //-- Kontrola delky prijateho tokenu
    if (rx_token.rx_ok) {
   		rx_token.rx_ok = (rx_token.batch*16) == rx_token.total_words;
    }

    //------------------------------------------------------------------------------
    //  Zpracovani tokenu
    //------------------------------------------------------------------------------

    if (rx_token.rx_ok) {
    	sendStringUART1("--- OK: ");
    	LED3_On();
    }
    else {
    	sendStringUART1("--- ERROR");
        if (rx_token.header_ok) {
        	sendStringUART1("(HEAD:OK):");
        	LED3_On();
        }
        else {
        	sendStringUART1("(HEAD:ERR):");
        }

    }

    //--- Vypise hlavicku

    switch (rx_token.token_type) {
		case SYSTEM_TOKEN:
			sendStringUART1("SYSTEM ");
			break;
		case NORMAL_TOKEN:
			sendStringUART1("NORMAL ");
			break;
		case TEST_TOKEN:
			sendStringUART1("PP-TEST ");
			break;
	}

	sprintf(buf,"NET=%02u DAU=%02u ADR=%02u PATH=%u ",rx_token.net,rx_token.dau,rx_token.adr,rx_token.path);
    sendStringUART1(buf);
	sprintf(buf,"TOKEN=%u BATCH=%u MASTER=%02u ",rx_token.token_id,rx_token.batch,rx_token.master);
    sendStringUART1(buf);

    //    sprintf(buf, "KALIBR:%lu\r\n",calib_count_per_bit);
    // Výpočet v milihertzech pomocí celých čísel
    uint32_t freq_mHz = (72000000ULL * 1000) / calib_count_per_bit;
    sprintf(buf, "f=%lu.%03luHz\r\n", freq_mHz / 1000, freq_mHz % 1000);
    sendStringUART1(buf);

	sendStringUART1("    DISTR=");
    switch (rx_token.distribution) {
		case DIRECT_TOKEN:
			sendStringUART1("DIRECT ");
			break;
		case REPAIR_TOKEN:
			sendStringUART1("REPAIR ");
			break;
		case REVERSE_TOKEN:
			sendStringUART1("REVERSE ");
			break;
	}
	sprintf(buf,"PASS-DAU=%02u ALARM-DAU=%02u ALARM-NO=%u\r\n",rx_token.pass_dau,rx_token.alarm_dau,rx_token.alarm_no);
    sendStringUART1(buf);

    sendStringUART1("    ");
    show_rtc();

    //--- Dekódování adresy a textu --- az od ctvrteho codewordu, za hlavickou
	if (rx_token.rx_ok) {
		if(rx_token.token_type == NORMAL_TOKEN) {

			textMsg[0] = '\0';
			bitBuffer = 0;
			bitsInBuffer = 0;
			bool has_address = false;

			for (uint16_t i = 3; i < rx_token.total_words; i++) {
				uint32_t raw = rx_token.data[i];
				if (raw == POCSAG_IDLE_WORD) continue;

				bool fixed = false;
				uint32_t clean = try_fix_word(raw, &fixed);
				if (calculate_syndrom(clean) != 0 || !check_parity(clean)) continue;

				if ((clean & 0x80000000) == 0) {
					// Nová adresa - vypsat zprávu předchozí adresy
					if (has_address) {
						sendStringUART1("MSG=");
						sendStringUART1(textMsg[0] != '\0' ? textMsg : "(prazdna)");
						sendStringUART1("\r\n");
					}

					// Výpočet úplné RIC adresy
					uint8_t wordInBatchPos = i % 16;
					uint8_t frameIndex = wordInBatchPos / 2;
					uint32_t addrPart = (clean >> 13) & 0x3FFFF;
					uint32_t fullRIC = (addrPart << 3) | (frameIndex & 0x07);
					uint8_t func = (clean >> 11) & 0x03;

					sprintf(buf, "    ADR=%07lu FCE=%d ", (unsigned long)fullRIC, func);
					sendStringUART1(buf);

					// Reset bufferu pro zprávu nové adresy
					textMsg[0] = '\0';
					bitBuffer = 0;
					bitsInBuffer = 0;
					has_address = true;
				}
				else {
					// Datové slovo - dekóduj do textMsg
					if (has_address) {
						decode_ascii_part(clean, textMsg);
					}
				}
			}

			// Vypsat zprávu poslední adresy
			if (has_address) {
				sendStringUART1("MSG=");
				sendStringUART1(textMsg[0] != '\0' ? textMsg : "(prazdna)");
				sendStringUART1("\r\n");
			}
		}
	}

	sendStringUART1("--- END ---\r\n");

	rx_token.ready = false;

	//------------------------------------------------------------------------------
    //  Vysilani
	//------------------------------------------------------------------------------
    if(rx_token.token_type != TEST_TOKEN) {

		if (rx_token.rx_ok)   //-- jen kompletne prijate tokeny
		{
			if (rx_token.adr == param.netdau[rx_token.net-1])   //-- je pro mne
			{
				//-- zjisti komu vysilat
				if (0==make_tx_route(rx_token.net, rx_token.path, rx_token.dau)) {

					//-- Vysilam
					LED4_On();
					tx_token = rx_token;
					if (tx_token.distribution == REVERSE_TOKEN) {
						tx_token.adr = tx_route.revers;  //-- Posle reverzni cestou
					}
					else {
						tx_token.adr = tx_route.follow;  //-- Posle primou cestou
					}
					tx_token.dau = param.netdau[rx_token.net-1];

					//-- Nastavi cekani na potvrzeni tokenu (jen pro REPAIR tokeny)
					if (tx_token.distribution == REPAIR_TOKEN) {
						route_state = WAIT_FOLLOW;
						route_repeat_counter = param.next_rpt+1;
						route_timer = param.next_time+1;
					}

					tx_token.tx_status = TX_DIRECT_WAY;
					tx_start();  //-- Spusti vysilani
				}
				else {
					sendStringUART1("NEVYSILAM - Nenalezen zaznam v ROUTE TABLE\r\n");
				}

			}
			else {  //-- token neni pro mne, kontrola routingu
				sendStringUART1("NEVYSILAM\r\n");

				if (route_state == WAIT_FOLLOW || route_state == WAIT_ERROR) {
					if (rx_token.net == tx_token.net && rx_token.dau == tx_token.adr) {
						//-- je to ten co cekam
						route_state = STATE_ROUTE_IDLE;
						LED4_Off();
						sendStringUART1("Token potvrzen\r\n");
					}
				}
			}
		}
    }
    else {
    	//-- TO DO zpracovani PP Testu - vysilat odpoved
    	if (rx_token.header_ok){
			sendStringUART1("TO DO: vysilat odpoved na PP-TEST\r\n");
    	}
    }
	LED3_Off();
}

//------------------------------------------------------------------------------
// Kontrola opakovani tokenu, pripadne Tx chybovou / reverzni cestou
//------------------------------------------------------------------------------
void POCSAG_routing_handler(void) {
	if(rx_state == STATE_RX_IDLE) { //-- Pokud prijima nebo vysila, tak nic nedelej
		switch (route_state) {
			case STATE_ROUTE_IDLE:
				break;

			case WAIT_FOLLOW:
				route_timer--;
				if (route_timer==0) {
					route_repeat_counter--;
					if (route_repeat_counter==0) {
						//-- Konec opakovani primou cestou, opakuje chybovou
						route_state = WAIT_ERROR;
						tx_token.adr = tx_route.error;
						route_repeat_counter = param.error_rpt+1;
						route_timer = param.next_time+1;
						sendStringUART1("ERROR-PATH\r\n");
						tx_token.tx_status = TX_ERROR_WAY;
						tx_start();
					}
					else {
						//-- opakuje primou cestou
						route_timer = param.next_time+1;
						sendStringUART1("REPEAT\r\n");
						tx_token.tx_status = TX_REPEAT_DIRECT;
						tx_start();
					}
				}
				break;

			case WAIT_ERROR:
				route_timer--;
				if (route_timer==0) {
					route_repeat_counter--;
					if (route_repeat_counter==0) {
						//-- Konec opakovani chybovou cestou, posle REVERSAL
						route_state = STATE_ROUTE_IDLE;  //-- nebude cekat
						tx_token.adr = tx_route.revers;
						tx_token.distribution = REVERSE_TOKEN;
						route_repeat_counter = 0;
						route_timer = 0;
						sendStringUART1("REVERSE TOKEN\r\n");
						tx_token.tx_status = tx_token.tx_status + TX_REVERSAL_WAY;
						tx_start();
					}
					else {
						//-- opakuje chybovou cestou
						route_timer = param.next_time+1;
						sendStringUART1("REPEAT ERROR\r\n");
						tx_token.tx_status = TX_ERROR_WAY;
						tx_start();
					}
				}
				break;

//			case WAIT_REVERS:
//				break;
		}
	}
}
