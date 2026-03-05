/******************************************************************************
 * @file pocsag.h
 * @brief Prijem a dekodovani POCSAG datagramu z PA0 (1200 bps)
 * @note Vzorkovani zajistuje TIMER1 bezici na 2400 Hz (2x rychlost bitu)
 * @note Synchronizace na hranu signalu pres GPIO preruseni na PA0
 *****************************************************************************/

#ifndef POCSAG_H
#define POCSAG_H

#include <stdint.h>

/******************************************************************************
 * Konstanty POCSAG protokolu
 *****************************************************************************/
#define POCSAG_SYNC_WORD            0x7CD215D8UL
#define POCSAG_IDLE_WORD            0x7A89C197UL
#define POCSAG_PREAMBLE_MIN_MATCH   28   /* min. shod z 32 bitu preamble     */
#define POCSAG_FRAMES_PER_BATCH     8
#define POCSAG_WORDS_PER_FRAME      2
#define POCSAG_WORDS_PER_BATCH      (POCSAG_FRAMES_PER_BATCH * POCSAG_WORDS_PER_FRAME)
#define POCSAG_MAX_MSG_LEN          80
#define POCSAG_MAX_CODEWORDS        64

/* Typy codeword */
#define POCSAG_CW_ADDRESS           0
#define POCSAG_CW_MESSAGE           1

/* Vysledky BCH kontroly */
#define POCSAG_BCH_OK               0
#define POCSAG_BCH_CORRECTED        1
#define POCSAG_BCH_ERROR            2

/* Frekvence TIMER1 - 2x rychlost bitu pro vzorkovani v pulce bitu */
#define POCSAG_TIMER1_FREQ          2400UL

/******************************************************************************
 * Stavovy automat prijimace
 *****************************************************************************/
typedef enum {
    POCSAG_STATE_IDLE = 0,  /* cekame na preamble                           */
    POCSAG_STATE_SYNC,      /* preamble nalezena, hledame sync word          */
    POCSAG_STATE_DATA       /* prijimame codewords                           */
} pocsag_state_t;

/******************************************************************************
 * Datove struktury
 *****************************************************************************/

/* Jeden prijaty codeword s vysledkem BCH */
typedef struct {
    uint32_t raw;           /* hodnota po pripadne BCH oprave               */
    uint8_t  type;          /* POCSAG_CW_ADDRESS nebo POCSAG_CW_MESSAGE      */
    uint8_t  bch_result;    /* POCSAG_BCH_OK / CORRECTED / ERROR             */
} pocsag_codeword_t;

/* Kompletni prijaty datagram */
typedef struct {
    uint32_t          address;                       /* RIC adresa pageru    */
    uint8_t           function;                      /* funkcni bity (0-3)   */
    char              message[POCSAG_MAX_MSG_LEN];   /* dekodovana zprava     */
    uint8_t           msg_len;                       /* delka zpravy          */
    uint8_t           codeword_count;                /* pocet codewords       */
    uint8_t           bch_errors;                    /* pocet neopr. chyb     */
    uint8_t           bch_corrections;               /* pocet BCH oprav       */
    uint8_t           valid;                         /* 1 = datagram je platny*/
    pocsag_codeword_t codewords[POCSAG_MAX_CODEWORDS];
} pocsag_datagram_t;

/******************************************************************************
 * Rozhrani
 *****************************************************************************/

/* Inicializace POCSAG prijimace vcetne GPIO preruseni na PA0 */
void    POCSAG_Init(void);

/* Volat z TIMER1_IRQHandler - bezi na 2400 Hz */
void    POCSAG_SampleBit(void);

/* Volat z GPIO_EVEN_IRQHandler - detekce hrany na PA0 */
void    POCSAG_EdgeDetected(void);

/* Vraci 1 pokud byl prijat kompletni datagram pripraveny ke cteni */
uint8_t POCSAG_DataReady(void);

/* Zkopiruje prijaty datagram do *dst a vynuluje priznak DataReady */
void    POCSAG_GetDatagram(pocsag_datagram_t *dst);

/* BCH(31,21) kontrola a jednobitkova oprava; meni codeword in-place */
uint8_t POCSAG_BCH_Check(uint32_t *codeword);

/* Vypis datagramu na UART1 */
void    POCSAG_PrintDatagram(const pocsag_datagram_t *dg);

#endif /* POCSAG_H */
