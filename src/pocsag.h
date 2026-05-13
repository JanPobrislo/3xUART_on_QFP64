#ifndef POCSAG_H
#define POCSAG_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_BATCHES      10
#define WORDS_PER_BATCH  16
#define POCSAG_SYNC_WORD 0x7CD215D8  // FS t.j. synchronizacni slovo
#define POCSAG_IDLE_WORD 0x7A89C197  // 01111010100010011100000110010111

typedef struct {
    uint32_t data[MAX_BATCHES * WORDS_PER_BATCH];
    uint16_t total_words;
    unsigned char timeout;  // Pouze pro master token z PC
//    volatile bool rx_end;    // Dokoncen prijem tokenu
    bool rx_ok;             // Token prijat bezchybne nebo chyby opraveny
    bool header_ok;         // Hlavicka tokenu (3xCDW] prijat bezchybne nebo chyby opraveny
    //------------------------------ Hlavicka prijateho POCSAG tokenu.
	unsigned char batch;	// Pocet batch - udaj uvedeny v hlavicce tokenu (nikoliv prijatych)
	unsigned char net;		// Cislo site
	unsigned char token_id;	// Cislo tokenu (TokenID = 1-31)
	unsigned char adr;		// Adresat (DAU) - komu je token posilan
	unsigned char dau;		// Odesilatel (DAU) - vysilac ktery vyslal tento token
	unsigned char path;	    // Radiova cesta (0-15)
	unsigned char master;	// Master DAU, ktery zahajil vysilani tokenu
	unsigned char token_type;	// 000=normal, 001=sytemovy token, 010=testovaci token (PP test)
	unsigned char distribution; // 00=direct(nepotvrzovany), 10=repair(potvrzovany), 01=reverzal
	unsigned char pass_dau;		// vysilac ktery nevyslal (byl obejit)
	unsigned char alarm_dau;	// vysilac ktery hlasi poruchu
	unsigned char alarm_no;	    // cislo poruchu
    //------------------------------ Neni v hlavicce tokenu, ale v tele system.tokenu
	unsigned char tx_status;    // transmit status
} POCSAG_token;

//-- typy tokenu
#define NORMAL_TOKEN 0  // 000
#define SYSTEM_TOKEN 1  // 001
#define TEST_TOKEN   2  // 010

//-- distribuce tokenu
#define DIRECT_TOKEN  0  // 00
#define REPAIR_TOKEN  2  // 10
#define REVERSE_TOKEN 1  // 01

//-- transmit status
#define TX_NO_TRANSMIT   0  // 000
#define TX_DIRECT_WAY  	 1  // 001
#define TX_REPEAT_DIRECT 2  // 010
#define TX_ERROR_WAY  	 3  // 011
#define TX_REVERSAL_WAY  4  // 1xx - nemeni bity 1. a 2.

//-- cislo poruchy DAU v hlavicce tokenu (alarm_no)
//   sdruzuje VZNIK=prvni bit + cislo poruchy
#define ERROR_NO_TAMPER_START  0xC  // 1100
#define ERROR_NO_TAMPER_END    0x4  // 0100
#define ERROR_NO_BATERY_START  0xB  // 1011
#define ERROR_NO_BATERY_END    0x3  // 0011

//extern POCSAG_token rx_token;

typedef enum {
    MASTER_IDLE,	 // klidovy stav
	MASTER_PREPARED, // token nacten zmasteru
	MASTER_SENT,	 // token odvysilan
	MASTER_RETURNED, // prijat token = obehl dokola
	MASTER_CONFIRMED // potvrzeno prijeti do masteru
} type_MASTER_State;

extern type_MASTER_State master_state;


void POCSAG_rx_init(void);
void POCSAG_edge_detected(void); // volano interuptem GPIO_EVEN_IRQHandler()
void POCSAG_sample_bit(void);    // volano z TIMER1 (1200 Hz)
void POCSAG_process(void);       // volano v main loop
//void POCSAG_Tx_datagram(void);
void POCSAG_show_rx_state(void);
void tx_start(void);
void POCSAG_routing_handler(void);
void master_token_loaded(void);
void MASTER_process(void);

#endif
