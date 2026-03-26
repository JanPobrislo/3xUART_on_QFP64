#ifndef POCSAG_H
#define POCSAG_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_BATCHES      10
#define WORDS_PER_BATCH  16
#define POCSAG_SYNC_WORD 0x7CD215D8
#define POCSAG_IDLE_WORD 0x7A89C197

typedef struct {
    uint32_t data[MAX_BATCHES * WORDS_PER_BATCH];
    uint16_t total_words;
    volatile bool ready;    // Dokoncen prijem tokenu
    bool rx_ok;             // Token prijat bezchybne nebo chyby opraveny
    //------------------------------ Hlavicka prijateho POCSAG tokenu.
	unsigned char batch;	// Pocet batch - udaj uvedeny v hlavicce tokenu (nikoliv prijatych)
	unsigned char net;		// Cislo site
	unsigned char token_id;	// Cislo tokenu (TokenID = 1-31)
	unsigned char adr;		// Adresat (DAU) - komu je token posilan
	unsigned char dau;		// Odesilatel (DAU) - vysilac ktery vyslal tento token
	unsigned char path;	    // Radiova cesta (0-15)
	unsigned char master;	// Master DAU, ktery zahajil vysilani tokenu
	unsigned char system_token;	// =1 pro sytemovy token
} POCSAG_token;

extern volatile POCSAG_token rx_token;

void POCSAG_Init(void);
void POCSAG_EdgeDetected(void); // Pro synchronizaci na začátku
void sample_bit(void);    // Voláno z TIMER1 (1200 Hz)
void POCSAG_Process(void);      // Výpis v main loop
void POCSAG_Tx_datagram(void);  // Vysle datagram
void tx_start(void);

#endif
