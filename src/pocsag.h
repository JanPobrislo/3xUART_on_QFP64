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
    volatile bool ready;
    //------------------------------ Hlavicka prijateho POCSAG tokenu.
	unsigned char Tbatch;	// Pocet batch - udaj uvedeny v hlavicce tokenu (nikoliv prijatych)
	unsigned char Tnet;		// Cislo site
	unsigned char Ttoken;	// Cislo tokenu (TokenID = 1-31)
	unsigned char Tadr;		// Adresat (DAU) - komu je token posilan
	unsigned char Tdau;		// Odesilatel (DAU) - vysilac ktery vyslal tento token
	unsigned char Tpath;	// Radiova cesta (0-15)
	unsigned char Tmaster;	// Master DAU, ktery zahajil vysilani tokenu
	unsigned char Tsystem;	// =1 pro sytemovy token
} POCSAG_Message;

extern volatile POCSAG_Message currentMsg;

void POCSAG_Init(void);
void POCSAG_EdgeDetected(void); // Pro synchronizaci na začátku
void POCSAG_SampleBit(void);    // Voláno z TIMER1 (1200 Hz)
void POCSAG_Process(void);      // Výpis v main loop

#endif
