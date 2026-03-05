/******************************************************************************
 * @file pocsag.c
 * @brief Prijem a dekodovani POCSAG datagramu z PA0 (1200 bps)
 *
 * Architektura vzorkovani:
 * ========================
 *   TIMER1 bezi na 2400 Hz (= 2x rychlost bitu 1200 bps).
 *   GPIO preruseni na PA0 detekuje kazdou hranu signalu a okamzite
 *   nastavi TIMER1->CNT = TOP/2, cimz zajisti ze pristi overflow
 *   TIMER1 nastane presne za pul bitu = ve stredu bitu.
 *   sample_tick se nastavi na 1 = pristi overflow se vzorkuje.
 *
 *   Casovani:
 *
 *   Signal:   ________|‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾|________
 *                     ^hrana
 *                     GPIO IRQ: CNT = TOP/2, sample_tick = 1
 *   TIMER1:           |---TOP/2---|---TOP---|---TOP---|
 *                                 ^ vzorkuj ^ vzorkuj
 *                                  (stred)   (stred)
 *
 * Synchronizace preamble:
 * =======================
 *   Klouzave 32-bitove okno porovnava shift_reg s fyzickym vzorem
 *   preamble 0xAAAAAAAA nebo 0x55555555.
 *   Staci 28 shod z 32 bitu (tolerance 4 bity).
 *   Po detekci preamble se shift_reg NERESETUJE - sync word muze
 *   uz castecne vstupovat do shift_reg.
 *
 * Signal na pinu:
 * ===============
 *   Signal je primy NRZ - fyzicke bity odpovidaji logickym hodnotam.
 *   Sync word na pinu = 0x7CD215D8 (stejne jako logicka hodnota).
 *   Zadne NRZ-S dekodovani neni potreba.
 *
 * BCH(31,21):
 * ===========
 *   Generator polynom: x^10+x^9+x^8+x^6+x^3+x+1 = 0x769
 *   Jednobitkova oprava in-place, kontrola celkove parity (bit 0).
 *****************************************************************************/

#include "pocsag.h"
#include "ports.h"
#include "uart1.h"
#include "em_gpio.h"
#include "em_cmu.h"

#include <string.h>
#include <stdio.h>

/*---------------------------------------------------------------------------*/
/* BCH(31,21) konstanty                                                       */
/*---------------------------------------------------------------------------*/
#define BCH_POLY        0x769UL   /* x^10+x^9+x^8+x^6+x^3+x+1              */
#define BCH_BITS        10        /* pocet kontrolnich bitu                  */

/*---------------------------------------------------------------------------*/
/* Interni stavove promenne                                                   */
/*---------------------------------------------------------------------------*/
static pocsag_state_t    state          = POCSAG_STATE_IDLE;

/* Posuvny registr a pocitadlo bitu */
static uint32_t          shift_reg      = 0;
static uint8_t           bit_count      = 0;

/* Fazovani vzorkovani: strida 0/1, vzorkujeme jen kdyz == 1 */
static volatile uint8_t  sample_tick    = 0;

/* Index codewordu v aktualnim batchi (0..15) */
static uint8_t           word_index     = 0;

/* Pracovni buffer prave prijimaneho datagramu */
static pocsag_datagram_t rx_dg;

/* Hotovy datagram cekajici na hlavni smycku */
static pocsag_datagram_t ready_dg;
static volatile uint8_t  data_ready     = 0;

/* Ladici promenne */
static volatile uint32_t edge_count     = 0;

/*---------------------------------------------------------------------------*/
/* Lokalni prototypy                                                          */
/*---------------------------------------------------------------------------*/
static void     process_codeword(uint32_t cw, uint8_t frame_pos);
static void     decode_message(void);
static void     finish_datagram(void);
static uint32_t bch_syndrome(uint32_t cw);

/******************************************************************************
 * @brief Inicializace POCSAG prijimace
 * @note  Konfiguruje GPIO preruseni na PA0 pro detekci hran signalu.
 *        TIMER1 musi byt inicializovan na 2400 Hz pred volanim teto funkce.
 *****************************************************************************/
void POCSAG_Init(void)
{
    state       = POCSAG_STATE_IDLE;
    shift_reg   = 0;
    bit_count   = 0;
    sample_tick = 0;
    word_index  = 0;
    data_ready  = 0;
    edge_count  = 0;
    memset(&rx_dg,    0, sizeof(rx_dg));
    memset(&ready_dg, 0, sizeof(ready_dg));

    /* Nastaveni PA0 jako vstup s pull-down filtrem */
    CMU_ClockEnable(cmuClock_GPIO, true);
    GPIO_PinModeSet(RX_PORT, RX_PIN, gpioModeInputPullFilter, 0);

    /* GPIO preruseni na PA0 - detekce obou hran (rising + falling) */
    GPIO_IntConfig(RX_PORT, RX_PIN,
                   true,    /* rising edge  */
                   true,    /* falling edge */
                   true);   /* enable       */

    /* PA0 = pin 0 = cislo preruseni EVEN */
    NVIC_ClearPendingIRQ(GPIO_EVEN_IRQn);
    NVIC_EnableIRQ(GPIO_EVEN_IRQn);

    sendStringUART1("POCSAG init OK - cekam na signal na PA0\r\n");
}

/******************************************************************************
 * @brief Obsluha GPIO preruseni - detekce hrany na PA0
 * @note  Volat z GPIO_EVEN_IRQHandler() v main.c
 *
 * Nastavi TIMER1->CNT = TOP/2 takze pristi overflow TIMER1 nastane
 * presne za pul bitu od teto hrany = vzorkujeme ve stredu bitu.
 * sample_tick = 1: pristi overflow se skutecne vzorkuje.
 * Ve stavu DATA hranu ignorujeme - jsme jiz synchronizovani.
 *****************************************************************************/
void POCSAG_EdgeDetected(void)
{
    edge_count++;

    if (state == POCSAG_STATE_IDLE || state == POCSAG_STATE_SYNC) {
        /* Resynchronizace: pristi vzorkovaci tick bude presne za pul bitu */
        TIMER1->CNT = TIMER1->TOP >> 1;
        sample_tick = 1;   /* pristi overflow = vzorkuj (ne preskoc)       */

        /* Ladici vypis prvnich 10 hran */
        if (edge_count <= 10) {
            char buf[48];
            sprintf(buf, "EDGE #%lu state=%u\r\n",
                    (unsigned long)edge_count, (uint8_t)state);
            sendStringUART1(buf);
        }
        if (edge_count == 11) {
            sendStringUART1("EDGE ... (dalsi nevypisuji)\r\n");
        }
    }
}

/******************************************************************************
 * @brief Vzorkovani bitu - volat z TIMER1_IRQHandler, bezi na 2400 Hz
 *
 * Kazdy druhy tick skutecne vzorkuje (= efektivne 1200 Hz).
 * Fazovani je resynchronizovano GPIO hranou tak aby vzorkovani
 * padalo do stredu bitu.
 * Signal je primy NRZ - fyzicke bity = logicke hodnoty, bez dekodovani.
 *****************************************************************************/
void POCSAG_SampleBit(void)
{
    /* Striedame ticky: 0=preskoc, 1=vzorkuj */
    sample_tick ^= 1;
    if (sample_tick == 0) return;

    /* Precti fyzicky stav PA0 - primy NRZ, bez dekodovani */
    uint8_t pin = (uint8_t)GPIO_PinInGet(RX_PORT, RX_PIN);

    /* Posun fyzickeho bitu do shift registru, MSB first */
    shift_reg = (shift_reg << 1) | pin;
    bit_count++;

    /*----------------------------------------------------------------------*/
    switch (state)
    {
    /*----------------------------------------------------------------------*/
    case POCSAG_STATE_IDLE:
    /*----------------------------------------------------------------------*/
        if (bit_count < 32) break;

        {
            /* Fyzicka preamble = strídání 10101010... = 0xAAAAAAAA
             * nebo                01010101... = 0x55555555
             * Tolerujeme 4 chybne bity = min 28 shod z 32                 */
            uint8_t match_a = (uint8_t)__builtin_popcount(
                                ~(shift_reg ^ 0xAAAAAAAAUL) & 0xFFFFFFFFUL);
            uint8_t match_b = (uint8_t)__builtin_popcount(
                                ~(shift_reg ^ 0x55555555UL) & 0xFFFFFFFFUL);
            uint8_t match   = (match_a > match_b) ? match_a : match_b;

            /* Ladici vypis - pouze pri zmene hodnoty match */
            static uint8_t last_match = 0;
            if (match != last_match) {
                char buf[48];
                sprintf(buf, "IDLE: match=%u/32 sr=%08lX\r\n",
                        match, (unsigned long)shift_reg);
//                sendStringUART1(buf);
                last_match = match;
            }

            if (match >= POCSAG_PREAMBLE_MIN_MATCH) {
                sendStringUART1(">> PREAMBLE OK - hledam sync\r\n");
                state = POCSAG_STATE_SYNC;
                /* NERESETUJEME shift_reg ani bit_count -                  */
                /* sync word uz muze castecne vstupovat do shift_reg       */
            }
        }
        break;

    /*----------------------------------------------------------------------*/
    case POCSAG_STATE_SYNC:
    /*----------------------------------------------------------------------*/
        {
            char buf[48];

            /* Ladici vypis kazdych 8 bitu */
            static uint8_t sync_print_cnt = 0;
            if (++sync_print_cnt >= 8) {
                sync_print_cnt = 0;
                sprintf(buf, "SYNC: sr=%08lX bc=%u hledam=%08lX\r\n",
                        (unsigned long)shift_reg,
                        bit_count,
                        (unsigned long)POCSAG_SYNC_WORD);
                sendStringUART1(buf);
            }

            /* Klouzave okno - sync word muze byt na libovolne bitove pozici */
            if (shift_reg == POCSAG_SYNC_WORD) {
                sendStringUART1(">> SYNC OK\r\n");
                state      = POCSAG_STATE_DATA;
                bit_count  = 0;
                shift_reg  = 0;
                word_index = 0;
                memset(&rx_dg, 0, sizeof(rx_dg));
                break;
            }

            /* Timeout: sync neprisel do 96 bitu za preamble -> zpet IDLE  */
            if (bit_count > 96) {
                sprintf(buf, ">> SYNC TIMEOUT sr=%08lX\r\n",
                        (unsigned long)shift_reg);
                sendStringUART1(buf);
                state     = POCSAG_STATE_IDLE;
                bit_count = 0;
                shift_reg = 0;
            }
        }
        break;

    /*----------------------------------------------------------------------*/
    case POCSAG_STATE_DATA:
    /*----------------------------------------------------------------------*/
        if (bit_count < 32) break;

        bit_count = 0;
        {
            uint32_t cw = shift_reg;
            shift_reg   = 0;
            char     buf[64];

            /* Ladici vypis prijateho codewordu */
            sprintf(buf, "DATA[%02u]: cw=%08lX\r\n",
                    word_index, (unsigned long)cw);
            sendStringUART1(buf);

            if (cw == POCSAG_SYNC_WORD) {
                /* Zacatek noveho batche */
                sendStringUART1(">> BATCH SYNC\r\n");
                word_index = 0;

            } else if (cw == POCSAG_IDLE_WORD) {
                /* Konec zpravy */
                sendStringUART1(">> IDLE WORD - konec zpravy\r\n");
                if (rx_dg.valid) {
                    finish_datagram();
                }

            } else {
                /* Datovy nebo adresovy codeword */
                uint8_t  cw_type = (cw >> 31) & 1;
                uint32_t cw_test = cw;
                uint8_t  bch     = POCSAG_BCH_Check(&cw_test);

                sprintf(buf, "  typ=%s BCH=%s\r\n",
                        cw_type ? "MSG" : "ADDR",
                        bch == POCSAG_BCH_OK        ? "OK"       :
                        bch == POCSAG_BCH_CORRECTED ? "OPRAVENO" : "CHYBA");
                sendStringUART1(buf);

                process_codeword(cw, word_index);
                word_index++;

                if (word_index >= POCSAG_WORDS_PER_BATCH) {
                    word_index = 0;
                }
            }
        }
        break;

    /*----------------------------------------------------------------------*/
    default:
    /*----------------------------------------------------------------------*/
        state = POCSAG_STATE_IDLE;
        break;
    }
}

/******************************************************************************
 * @brief Zpracovani jednoho codewordu
 * @param cw        32-bit codeword (primy NRZ, bez dekodovani)
 * @param frame_pos Poradi codewordu v batchi (0..15)
 *****************************************************************************/
static void process_codeword(uint32_t cw, uint8_t frame_pos)
{
    if (rx_dg.codeword_count >= POCSAG_MAX_CODEWORDS) return;

    pocsag_codeword_t *pcw = &rx_dg.codewords[rx_dg.codeword_count];

    /* BCH kontrola a pripadna oprava in-place */
    pcw->bch_result = POCSAG_BCH_Check(&cw);
    pcw->raw        = cw;

    if (pcw->bch_result == POCSAG_BCH_ERROR) {
        rx_dg.bch_errors++;
    } else if (pcw->bch_result == POCSAG_BCH_CORRECTED) {
        rx_dg.bch_corrections++;
    }

    /* Bit 31: 0 = adresovy codeword, 1 = datovy (message) codeword */
    if ((cw >> 31) == 0) {
        pcw->type = POCSAG_CW_ADDRESS;

        /* Idle word = konec zpravy */
        if (cw == POCSAG_IDLE_WORD) {
            if (rx_dg.valid) {
                finish_datagram();
            }
            return;
        }

        /* Adresovy codeword:
         * bity 30..13 = hornich 18 bitu RIC adresy
         * bity 12..11 = funkcni bity
         * dolni 3 bity adresy urceny pozici framu v batchi               */
        rx_dg.address  = ((cw >> 13) & 0x3FFFFUL) << 3;
        rx_dg.address |= (frame_pos / 2) & 0x07UL;
        rx_dg.function = (uint8_t)((cw >> 11) & 0x03);
        rx_dg.valid    = 1;

    } else {
        pcw->type = POCSAG_CW_MESSAGE;
    }

    rx_dg.codeword_count++;
}

/******************************************************************************
 * @brief Uzavreni datagramu - dekodovani zpravy a predani hlavni smycce
 *****************************************************************************/
static void finish_datagram(void)
{
    decode_message();
    memcpy(&ready_dg, &rx_dg, sizeof(pocsag_datagram_t));
    data_ready = 1;
    memset(&rx_dg, 0, sizeof(rx_dg));
    state     = POCSAG_STATE_IDLE;
    bit_count = 0;
    shift_reg = 0;
}

/******************************************************************************
 * @brief Dekodovani textove zpravy ze vsech MESSAGE codewordu
 *
 * Kazdy MESSAGE codeword obsahuje 20 datovych bitu (bity 30..11).
 * Znaky jsou 7-bit ASCII ulozene MSB-first, prochazejici hranicemi CW.
 *****************************************************************************/
static void decode_message(void)
{
    uint8_t  bit_buf[POCSAG_MAX_CODEWORDS * 20];
    uint16_t total_bits = 0;
    uint8_t  i, b;

    memset(bit_buf, 0, sizeof(bit_buf));

    /* Extrahuj datove bity ze vsech MESSAGE codewordu */
    for (i = 0; i < rx_dg.codeword_count; i++) {
        if (rx_dg.codewords[i].type != POCSAG_CW_MESSAGE) continue;
        uint32_t cw = rx_dg.codewords[i].raw;
        /* Bity 30 az 11 = 20 datovych bitu, MSB first */
        for (b = 30; b >= 11; b--) {
            if (total_bits < sizeof(bit_buf)) {
                bit_buf[total_bits++] = (uint8_t)((cw >> b) & 1);
            }
        }
    }

    /* Sestaveni 7-bit ASCII znaku z bitoveho proudu */
    rx_dg.msg_len = 0;
    uint16_t pos  = 0;

    while ((pos + 7) <= total_bits &&
           rx_dg.msg_len < (POCSAG_MAX_MSG_LEN - 1))
    {
        uint8_t ch = 0;
        for (b = 0; b < 7; b++) {
            ch = (uint8_t)((ch << 1) | bit_buf[pos + b]);
        }
        pos += 7;

        if (ch == 0x00) break;              /* EOT - konec zpravy          */
        if (ch >= 0x20 && ch < 0x7F) {     /* tisknutelny ASCII znak       */
            rx_dg.message[rx_dg.msg_len++] = (char)ch;
        }
    }
    rx_dg.message[rx_dg.msg_len] = '\0';
}

/******************************************************************************
 * @brief BCH(31,21) kontrola a jednobitkova oprava
 *
 * Algoritmus:
 *   1. Vypocti syndrom z bitu 31..1 (bit 0 je paritni)
 *   2. Syndrom == 0 a parita OK -> POCSAG_BCH_OK
 *   3. Zkus otocit kazdy bit 31..1 -> syndrom == 0 a parita OK -> CORRECTED
 *   4. Jinak -> POCSAG_BCH_ERROR
 *
 * @param codeword  Ukazatel na 32-bit hodnotu; opravena hodnota ulozena zpet
 * @return          POCSAG_BCH_OK / POCSAG_BCH_CORRECTED / POCSAG_BCH_ERROR
 *****************************************************************************/
uint8_t POCSAG_BCH_Check(uint32_t *codeword)
{
    uint32_t cw = *codeword;

    #define EVEN_PARITY(x)  ((__builtin_popcount(x) & 1) == 0)

    if (bch_syndrome(cw) == 0 && EVEN_PARITY(cw)) {
        return POCSAG_BCH_OK;
    }

    /* Zkus jednobitkovou opravu - projdi bity 31..1 */
    uint8_t bit;
    for (bit = 1; bit <= 31; bit++) {
        uint32_t test = cw ^ (1UL << bit);
        if (bch_syndrome(test) == 0 && EVEN_PARITY(test)) {
            *codeword = test;
            return POCSAG_BCH_CORRECTED;
        }
    }

    #undef EVEN_PARITY

    return POCSAG_BCH_ERROR;
}

/******************************************************************************
 * @brief Vypocet BCH syndromu pro bity 31..1 (paritni bit 0 se ignoruje)
 *****************************************************************************/
static uint32_t bch_syndrome(uint32_t cw)
{
    uint32_t rem = 0;
    int8_t   i;

    for (i = 31; i >= 1; i--) {
        rem <<= 1;
        if ((cw >> i) & 1) rem ^= 1;
        if (rem & (1UL << BCH_BITS)) rem ^= BCH_POLY;
    }
    return rem & ((1UL << BCH_BITS) - 1);
}

/******************************************************************************
 * @brief Vraci 1 pokud je pripraveny kompletni datagram ke cteni
 *****************************************************************************/
uint8_t POCSAG_DataReady(void)
{
    return data_ready;
}

/******************************************************************************
 * @brief Zkopiruje hotovy datagram do *dst a vynuluje priznak DataReady
 *****************************************************************************/
void POCSAG_GetDatagram(pocsag_datagram_t *dst)
{
    memcpy(dst, &ready_dg, sizeof(pocsag_datagram_t));
    data_ready = 0;
}

/******************************************************************************
 * @brief Vypis prijateho datagramu na UART1
 *****************************************************************************/
void POCSAG_PrintDatagram(const pocsag_datagram_t *dg)
{
    char buf[64];

    sendStringUART1("\r\n=== POCSAG datagram ===\r\n");

    if (!dg->valid) {
        sendStringUART1("  [neplatny datagram]\r\n");
        sendStringUART1("=======================\r\n");
        return;
    }

    sprintf(buf, "  Adresa  : %lu\r\n", (unsigned long)dg->address);
    sendStringUART1(buf);

    sprintf(buf, "  Funkce  : %u\r\n", dg->function);
    sendStringUART1(buf);

    sprintf(buf, "  Zprava  : %s\r\n", dg->message);
    sendStringUART1(buf);

    sprintf(buf, "  Delka   : %u znaku\r\n", dg->msg_len);
    sendStringUART1(buf);

    sprintf(buf, "  CW pocet: %u\r\n", dg->codeword_count);
    sendStringUART1(buf);

    if (dg->bch_errors == 0 && dg->bch_corrections == 0) {
        sendStringUART1("  BCH     : OK - bez chyb\r\n");
    } else if (dg->bch_errors == 0) {
        sprintf(buf, "  BCH     : OPRAVENO - %u oprav\r\n",
                dg->bch_corrections);
        sendStringUART1(buf);
    } else {
        sprintf(buf, "  BCH     : CHYBA - %u neopr., %u oprav\r\n",
                dg->bch_errors, dg->bch_corrections);
        sendStringUART1(buf);
    }

    sendStringUART1("=======================\r\n");
}
