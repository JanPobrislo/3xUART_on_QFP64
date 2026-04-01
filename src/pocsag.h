#ifndef POCSAG_H
#define POCSAG_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_BATCHES      10
#define WORDS_PER_BATCH  16
#define POCSAG_SYNC_WORD 0x7CD215D8  // FS t.j. synchronizacni slovo
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

//--- Kalibrace rychlosti prijmu
extern bool calib_start;
extern bool calib_stop;
extern uint32_t calib_start_counter;
extern uint32_t calib_stop_counter;
extern uint16_t calib_bits;

void POCSAG_rx_init(void);
void POCSAG_edge_detected(void); // volano interuptem GPIO_EVEN_IRQHandler()
void POCSAG_sample_bit(void);    // volano z TIMER1 (1200 Hz)
void POCSAG_process(void);       // volano v main loop
//void POCSAG_Tx_datagram(void);
void POCSAG_show_rx_state(void);
void tx_start(void);

#endif
