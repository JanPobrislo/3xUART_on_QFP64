/******************************************************************************
 * @file pocsag.h
 * @brief POCSAG dekodér - příjem a dekódování POCSAG datagramů na PA0
 *
 * Protokol POCSAG (Post Office Code Standardisation Advisory Group):
 *   - Rychlost: 1200 Bd (NRZ, již demodulováno)
 *   - Preamble: min. 576 bitů střídajících 1/0
 *   - Sync codeword: 0x7CD215D8 (před každým batchem)
 *   - Batch: 8 framů × 2 slova = 16 slov × 32 bitů
 *   - Každé slovo: 21 dat. bitů + 10 BCH bitů + 1 paritní bit (sudá parita)
 *   - BCH(31,21): generátor x^10+x^9+x^8+x^6+x^5+x^3+1 = 0x769
 *   - Zpráva může pokračovat přes VÍCE batchů za sebou (každý má vlastní
 *     sync word 0x7CD215D8 – bez nové preamble mezi nimi)
 *
 * Architektura bufferu:
 *   Přijatý batch se ukládá do kruhové fronty POCSAG_BatchQueue[].
 *   Main loop vybírá batche z fronty a zpracovává je nezávisle na příjmu.
 *   Fronta má kapacitu POCSAG_QUEUE_SIZE batchů – pokud je plná,
 *   nejstarší batch se přepíše (přijímač nesmí blokovat přerušení).
 *
 * Vzorkování:
 *   TIMER1 běží na 2400 Hz (2 vzorky / bit).
 *   GPIO_EVEN_IRQ na PA0 detekuje hranu → resetuje fázi na 1 (= půl doby bitu).
 *   Skutečné vzorkování bitu probíhá při fázi == 1 (uprostřed bitu).
 *****************************************************************************/
#ifndef POCSAG_H
#define POCSAG_H

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/*  Konstanty protokolu                                                       */
/*---------------------------------------------------------------------------*/
#define POCSAG_SYNC_WORD        0x7CD215D8UL  /* synchronizační slovo        */
#define POCSAG_IDLE_WORD        0x7A89C197UL  /* idle / nečinné slovo        */
#define POCSAG_PREAMBLE_BITS    32            /* min. střídavých bitů        */
#define POCSAG_WORDS_PER_BATCH  16            /* 8 framů × 2 slova           */

/* Kapacita fronty batchů – musí být mocnina 2 pro efektivní modulo */
#define POCSAG_QUEUE_SIZE       4             /* 4 batche ve frontě          */

/*---------------------------------------------------------------------------*/
/*  Typy                                                                      */
/*---------------------------------------------------------------------------*/

typedef enum {
    POCSAG_WORD_ADDRESS = 0,  /* adresní slovo (MSB = 0)                     */
    POCSAG_WORD_MESSAGE = 1,  /* datové slovo  (MSB = 1)                     */
    POCSAG_WORD_IDLE    = 2   /* idle slovo (0x7A89C197)                     */
} POCSAG_WordType_t;

typedef enum {
    POCSAG_BCH_OK       = 0,  /* slovo bez chyby                             */
    POCSAG_BCH_FIXED    = 1,  /* opravena 1-bitová chyba                     */
    POCSAG_BCH_ERROR    = 2,  /* neopravitelná chyba (>=2 bity)              */
    POCSAG_BCH_PARITY   = 3   /* chyba parity (lichý počet jedniček)         */
} POCSAG_BCHResult_t;

/** Jedno 32-bitové POCSAG slovo po dekódování */
typedef struct {
    uint32_t            raw;    /* surová hodnota z rádia                    */
    uint32_t            data;   /* 20 datových bitů (MESSAGE) nebo           */
                                /* 18 adres. bitů (ADDRESS), po BCH opravě  */
    POCSAG_WordType_t   type;
    POCSAG_BCHResult_t  bch;
    uint8_t             frame;  /* číslo framu v batchi (0..7)               */
    uint8_t             slot;   /* pozice ve framu (0 nebo 1)                */
} POCSAG_Word_t;

/** Jeden přijatý batch (16 slov) */
typedef struct {
    POCSAG_Word_t   words[POCSAG_WORDS_PER_BATCH];
    uint8_t         batch_num;  /* pořadové číslo batche v datagramu (0..)   */
    uint8_t         count;      /* počet uložených slov (0..16)              */
    uint8_t         errors;     /* počet slov s neopravitelnou chybou        */
    uint8_t         fixed;      /* počet opravených slov                     */
} POCSAG_Batch_t;

/** Stav stavového automatu dekodéru */
typedef enum {
    POCSAG_STATE_NOISE    = 0,  /* šum – čekáme na preamble                  */
    POCSAG_STATE_PREAMBLE = 1,  /* preamble detekována – hledáme sync word   */
    POCSAG_STATE_DATA     = 2,  /* přijímáme slova batche                    */
} POCSAG_State_t;

/*---------------------------------------------------------------------------*/
/*  Fronta batchů (kruhový buffer, plněný z ISR, čtený z main loopu)        */
/*---------------------------------------------------------------------------*/
extern volatile uint8_t  POCSAG_QueueHead;   /* index pro čtení (main loop) */
extern volatile uint8_t  POCSAG_QueueTail;   /* index pro zápis (ISR)       */
extern          POCSAG_Batch_t POCSAG_BatchQueue[POCSAG_QUEUE_SIZE];

/* Pomocná makra pro práci s frontou */
#define POCSAG_QUEUE_EMPTY()  (POCSAG_QueueHead == POCSAG_QueueTail)
#define POCSAG_QUEUE_FULL()   (((POCSAG_QueueTail + 1u) & (POCSAG_QUEUE_SIZE - 1u)) \
                               == POCSAG_QueueHead)

/*---------------------------------------------------------------------------*/
/*  Veřejné funkce                                                            */
/*---------------------------------------------------------------------------*/

/** Inicializace POCSAG dekodéru a GPIO přerušení na PA0.
 *  Volat po initTIMER1() z main(). */
void POCSAG_Init(void);

/** Volat z TIMER1_IRQHandler (2400 Hz) – vzorkuje bit, pohání stavový automat */
void POCSAG_SampleBit(void);

/** Volat z GPIO_EVEN_IRQHandler – synchronizuje fázi vzorkování na hranu signálu */
void POCSAG_EdgeDetected(void);

/** BCH(31,21) kontrola a případná oprava 1-bitové chyby.
 *  @param word       surové 32-bitové slovo (bit31=MSB=první přijatý bit, bit0=parita)
 *  @param fixed_out  výstupní opravené slovo
 *  @return výsledek kontroly */
POCSAG_BCHResult_t POCSAG_CheckBCH(uint32_t word, uint32_t *fixed_out);

/** Vyzvedne jeden batch z fronty a vypíše ho na UART1.
 *  @return 1 pokud byl batch zpracován, 0 pokud byla fronta prázdná.
 *  Volat opakovaně z main loopu dokud vrací 1. */
uint8_t POCSAG_ProcessQueue(void);

/** Vrátí textový název aktuálního stavu dekodéru (pro ladění) */
const char *POCSAG_GetStateName(void);

/** Diagnostický výpis na UART1 – volat 1× za sekundu z IsSecond bloku.
 *  Vypíše: stav, PA0, SampleBit/s, Bits/s, hrany/s, šum, preamble_max,
 *  shift_reg. Pomáhá zjistit proč se nedeteukje preamble. */
void POCSAG_PrintDiag(void);

/* Diagnostické čítače – přístup pro případ vlastního výpisu */
extern volatile uint32_t diag_sample_calls;
extern volatile uint32_t diag_bits_read;
extern volatile uint32_t diag_edge_calls;
extern volatile uint16_t diag_preamble_max;
extern volatile uint32_t diag_same_bits;

#endif /* POCSAG_H */
