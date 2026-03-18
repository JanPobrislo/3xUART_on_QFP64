/******************************************************************************
 * @file pocsag.c
 * @brief POCSAG dekodér – vzorkování, stavový automat, BCH(31,21), fronta batchů
 *
 * OPRAVY oproti předchozí verzi:
 * --------------------------------
 * 1. VÍCE BATCHŮ V JEDNOM DATAGRAMU
 *    POCSAG datagram se skládá z jedné preamble a pak libovolného počtu
 *    batchů. Každý batch začíná sync slovem 0x7CD215D8, ale mezi batchi
 *    NENÍ nová preamble. Po dokončení 16 slov batche proto přejdeme do
 *    nového stavu POCSAG_STATE_PREAMBLE, kde posuvný shift_reg sleduje
 *    příchod dalšího sync slova – a to buď ihned (další batch), nebo
 *    po nové preamble (nový datagram). Stav NOISE se použije jen pro
 *    počáteční synchronizaci po zapnutí / po ztrátě signálu.
 *
 * 2. KRUHOVÁ FRONTA BATCHŮ
 *    Každý dokončený batch se zapíše do fronty POCSAG_BatchQueue[] a
 *    ISR se okamžitě vrátí. Main loop volá POCSAG_ProcessQueue() a
 *    vyzvedává batche jeden po druhém. Tím se ISR nikdy nezablokuje
 *    výpisem na UART a main loop neztrácí batche ani při výpisu.
 *    Pokud je fronta plná, nejstarší batch se přepíše (zápis nečeká).
 *
 * 3. ČÍTAČ POŘADOVÝCH ČÍSEL BATCHŮ
 *    Proměnná batch_sequence_num sleduje, kolikátý batch v aktuálním
 *    datagramu právě přijímáme. Resetuje se při nové preamble (NOISE→
 *    PREAMBLE). Díky tomu main loop vidí, zda batche na sebe navazují.
 *
 * Vzorkování (beze změny):
 *   TIMER1 = 2400 Hz → 2 vzorky / bit. sample_phase střídá 0 / 1.
 *   Bit se čte pouze ve fázi 1 (střed bitu).
 *   POCSAG_EdgeDetected() nastavuje sample_phase = 1 (synchronizace).
 *
 * BCH(31,21) (beze změny):
 *   Generátor g(x) = 0x769. Syndrom = 0 → OK.
 *   Pokud syndrom != 0, zkusíme XOR každého z 31 bitů → oprava 1 chyby.
 *   Bit 0 = sudá parita celého 32-bitového slova.
 *****************************************************************************/

#include "pocsag.h"
#include "ports.h"
#include "uart1.h"
#include "led.h"

#include "em_gpio.h"
#include "em_cmu.h"

#include <string.h>
#include <stdio.h>

/*===========================================================================*/
/*  Interní konstanty                                                         */
/*===========================================================================*/

#define BCH_GENERATOR   0x769u   /* g(x) koeficienty bitů 10..0             */
#define BCH_WORD_BITS   31       /* počet bitů BCH codeword (bez parity)     */

/*===========================================================================*/
/*  Fronta batchů – definice (deklarace je v pocsag.h)                       */
/*===========================================================================*/

volatile uint8_t    POCSAG_QueueHead = 0;
volatile uint8_t    POCSAG_QueueTail = 0;
POCSAG_Batch_t      POCSAG_BatchQueue[POCSAG_QUEUE_SIZE];

/*===========================================================================*/
/*  Interní stav dekodéru                                                     */
/*===========================================================================*/

static POCSAG_State_t   state            = POCSAG_STATE_NOISE;
static volatile uint8_t sample_phase    = 0;
static uint32_t         shift_reg       = 0;
static uint8_t          last_bit        = 0xFF; /* 0xFF = neplatný           */
static uint16_t         preamble_count  = 0;
static uint8_t          bit_count       = 0;    /* bity aktuálního slova     */
static uint8_t          word_count      = 0;    /* slova v aktuálním batchi  */
static uint32_t         current_word    = 0;
static uint8_t          batch_seq       = 0;    /* pořadí batche v datagramu */

/* Diagnostické čítače – čtené přes POCSAG_PrintDiag() každou sekundu */
volatile uint32_t diag_sample_calls  = 0;  /* kolikrát zavolán SampleBit      */
volatile uint32_t diag_bits_read     = 0;  /* kolikrát skutečně přečten bit    */
volatile uint32_t diag_edge_calls    = 0;  /* kolikrát přišla hrana na PA0     */
volatile uint16_t diag_preamble_max  = 0;  /* max dosažené preamble_count      */
volatile uint32_t diag_same_bits     = 0;  /* počet "stejný bit 2x" = šum      */

/*===========================================================================*/
/*  Privátní prototypy                                                        */
/*===========================================================================*/
static void     process_word(uint32_t raw_word);
static void     finish_batch(void);
static void     reset_decoder(void);
static uint32_t bch_syndrome(uint32_t cw31);
static void     print_batch(const POCSAG_Batch_t *b);
static void     decode_message(const POCSAG_Batch_t *b, char *out, uint8_t out_size);

/*===========================================================================*/
/*  Inicializace                                                              */
/*===========================================================================*/

void POCSAG_Init(void)
{
    CMU_ClockEnable(cmuClock_GPIO, true);

    GPIO_PinModeSet(RX_PORT, RX_PIN, gpioModeInputPull, 1);

    /* Přerušení na obě hrany PA0 (RX_PIN = 0 → GPIO_EVEN) */
    GPIO_ExtIntConfig(RX_PORT,
                      RX_PIN,
                      RX_PIN,   /* intNo = 0 */
                      true,     /* risingEdge  */
                      true,     /* fallingEdge */
                      true);    /* enable      */

    NVIC_ClearPendingIRQ(GPIO_EVEN_IRQn);
    NVIC_EnableIRQ(GPIO_EVEN_IRQn);

    reset_decoder();
}

/*===========================================================================*/
/*  Synchronizace fáze – z GPIO_EVEN_IRQHandler                              */
/*===========================================================================*/

void POCSAG_EdgeDetected(void)
{
    /*
     * Hrana přišla = ZAČÁTEK nového bitu.
     *
     * Chceme číst bit uprostřed jeho trvání, tj. za 1/2 doby bitu = za
     * jeden TIMER1 tick (2400 Hz → tick = 1/2400 s = 1/2 doby bitu 1200 Bd).
     *
     * Logika v POCSAG_SampleBit():
     *   sample_phase ^= 1u;
     *   if (sample_phase == 0u) return;   ← přeskočí, čte jen když == 1
     *
     * Aby PRVNÍ tick po hraně byl čtecí (sample_phase po XOR == 1),
     * musíme nastavit sample_phase = 0  →  0 XOR 1 = 1  →  čteme.
     *
     * POZOR: dříve byl kód "= 1" což způsobovalo OPAČNÉ chování:
     *   1 XOR 1 = 0 → přeskočí (nečte), 0 XOR 1 = 1 → čte až druhý tick
     *   = čtení na konci bitu místo uprostřed.
     *
     * Během DATA stavu fázi NEpřepisujeme – synchronizace probíhá jen
     * ve stavech NOISE a PREAMBLE (hledání preamble a sync slova).
     */
    if (state != POCSAG_STATE_DATA) {
        sample_phase = 0u;   /* 0 XOR 1 = 1 → příští tick čte bit (střed bitu) */
    }
    diag_edge_calls++;
}

/*===========================================================================*/
/*  Vzorkování bitu – z TIMER1_IRQHandler (2400 Hz)                          */
/*===========================================================================*/

void POCSAG_SampleBit(void)
{
    diag_sample_calls++;

    /* Přepneme fázi 0↔1 */
    sample_phase ^= 1u;

    /* Čteme pouze ve fázi 1 (střed bitu) */
    if (sample_phase == 0u) return;

    diag_bits_read++;
    uint8_t bit = (uint8_t)GPIO_PinInGet(RX_PORT, RX_PIN);

    switch (state)
    {
    /*-----------------------------------------------------------------------*/
    case POCSAG_STATE_NOISE:
    /*
     * Hledáme střídavé bity preamble.
     * Podmínka přechodu: POCSAG_PREAMBLE_BITS po sobě jdoucích ZMĚN bitu.
     * Každý správný preamble bit je RŮZNÝ od předchozího.
     * Dva stejné bity za sebou = šum → reset čítače.
     */
        if (last_bit == 0xFFu) {
            /* První bit vůbec – jen si ho zapamatujeme */
            last_bit       = bit;
            preamble_count = 1u;
        } else if (bit != last_bit) {
            /* Střídání – dobrý preamble bit */
            last_bit = bit;
            preamble_count++;
            if (preamble_count > diag_preamble_max) diag_preamble_max = preamble_count;
            if (preamble_count >= POCSAG_PREAMBLE_BITS) {
                /* Preamble detekována */
                state          = POCSAG_STATE_PREAMBLE;
                shift_reg      = 0u;
                batch_seq      = 0u;
                /* last_bit necháme – PREAMBLE stav ho potřebuje pro
                   detekci nové preamble (sleduje střídání) */
                preamble_count = 0u;   /* reset pro PREAMBLE stav */
            }
        } else {
            /* Dva stejné bity za sebou = šum, začít znovu od tohoto bitu */
            diag_same_bits++;
            preamble_count = 1u;
            last_bit       = bit;
        }
        break;

    /*-----------------------------------------------------------------------*/
    case POCSAG_STATE_PREAMBLE:
    /*
     * Posuvný registr hledá sync slovo 0x7CD215D8.
     * Sem přicházíme:
     *   a) po preamble nového datagramu (batch_seq == 0, last_bit platný)
     *   b) po dokončení předchozího batche (batch_seq > 0)
     *
     * Sledujeme střídání bitů pro detekci případné nové preamble.
     * Novou preamble poznáme jako > 64 střídání — pak je to nový datagram.
     * Sync slovo 0x7CD215D8 střídání přeruší, preamble_count se resetuje.
     *
     * POZOR: last_bit může být 0xFFu po přechodu z DATA stavu.
     * V takovém případě první bit vždy projde jako "různý".
     */
    	LED1_On();
    	shift_reg = (shift_reg << 1u) | bit;

        if (last_bit == 0xFFu) {
            /* První bit po přechodu z DATA – jen inicializujeme */
            last_bit       = bit;
            preamble_count = 1u;
        } else if (bit != last_bit) {
            last_bit = bit;
            preamble_count++;
            if (preamble_count > 64u) {
                /* Nová preamble nového datagramu */
                batch_seq      = 0u;
                preamble_count = 0u;
            }
        } else {
            /* Přestalo se střídat – sync word nebo konec preamble */
            preamble_count = 0u;
        }

        if (shift_reg == POCSAG_SYNC_WORD) {
            /* Sync word nalezen → přejdeme do příjmu dat */
            state        = POCSAG_STATE_DATA;
            bit_count    = 0u;
            word_count   = 0u;
            current_word = 0u;
            uint8_t tail = POCSAG_QueueTail;
            memset(&POCSAG_BatchQueue[tail], 0, sizeof(POCSAG_Batch_t));
            POCSAG_BatchQueue[tail].batch_num = batch_seq;
        }
        break;

    /*-----------------------------------------------------------------------*/
    case POCSAG_STATE_DATA:
    /*
     * Přijímáme 16 slov × 32 bitů.
     * Po každém slově: BCH kontrola + uložení do fronty.
     * Po 16. slově: batch uzavřeme a přejdeme do PREAMBLE (čekáme na
     *   sync slovo dalšího batche nebo novou preamble nového datagramu).
     *   NEvracíme se do NOISE – signál stále přichází.
     */
    	LED3_On();
        current_word = (current_word << 1u) | bit;
        bit_count++;

        if (bit_count == 32u) {
            bit_count = 0u;
            process_word(current_word);
            current_word = 0u;
            word_count++;

            if (word_count >= POCSAG_WORDS_PER_BATCH) {
                /* Batch kompletní */
                finish_batch();
                batch_seq++;

                /* Přejdeme do PREAMBLE: čekáme na sync slovo dalšího batche
                   (ihned, bez nové preamble) nebo na nový datagram.
                   last_bit = 0xFFu signalizuje "první bit po DATA" –
                   PREAMBLE stav to správně zpracuje.                 */
                state          = POCSAG_STATE_PREAMBLE;
                shift_reg      = 0u;
                last_bit       = 0xFFu;
                preamble_count = 0u;
            }
        }
        break;

    default:
        reset_decoder();
        break;
    }
}

/*===========================================================================*/
/*  Zpracování jednoho přijatého slova                                        */
/*===========================================================================*/

static void process_word(uint32_t raw_word)
{
    uint8_t tail = POCSAG_QueueTail;
    uint8_t idx  = word_count;
    if (idx >= POCSAG_WORDS_PER_BATCH) return;

    POCSAG_Word_t *w = &POCSAG_BatchQueue[tail].words[idx];
    w->raw   = raw_word;
    w->frame = idx / 2u;
    w->slot  = idx % 2u;

    /* IDLE slovo */
    if (raw_word == POCSAG_IDLE_WORD) {
        w->type = POCSAG_WORD_IDLE;
        w->bch  = POCSAG_BCH_OK;
        w->data = 0u;
        POCSAG_BatchQueue[tail].count++;
        return;
    }

    /* BCH kontrola */
    uint32_t fixed = raw_word;
    w->bch = POCSAG_CheckBCH(raw_word, &fixed);

    switch (w->bch) {
    case POCSAG_BCH_FIXED:  POCSAG_BatchQueue[tail].fixed++;  break;
    case POCSAG_BCH_ERROR:
    case POCSAG_BCH_PARITY: POCSAG_BatchQueue[tail].errors++; break;
    default: break;
    }

    /* Typ slova dle MSB (bit 31) */
    if ((fixed >> 31u) & 1u) {
        /* MESSAGE slovo: bity 30..11 = 20 datových bitů */
        w->type = POCSAG_WORD_MESSAGE;
        w->data = (fixed >> 11u) & 0x000FFFFFu;
    } else {
        /* ADDRESS slovo: bity 30..13 = 18 adresních bitů, bity 12..11 = funkce */
        w->type = POCSAG_WORD_ADDRESS;
        w->data = (fixed >> 13u) & 0x0003FFFFu;
        /* Adresu a funkci uložíme do batche jen z prvního adresního slova */
        /* (dekódování adresy + funkce se provede v POCSAG_ProcessQueue)  */
    }

    POCSAG_BatchQueue[tail].count++;
}

/*===========================================================================*/
/*  Uzavření batche a jeho vložení do fronty                                  */
/*===========================================================================*/

static void finish_batch(void)
{
    /* Posuneme tail – batch je tím "zveřejněn" pro main loop.
       Pokud je fronta plná, přepíšeme nejstarší batch (head se posune). */
    uint8_t next_tail = (POCSAG_QueueTail + 1u) & (POCSAG_QUEUE_SIZE - 1u);

    if (next_tail == POCSAG_QueueHead) {
        /* Fronta plná – přepíšeme nejstarší (posuneme head) */
        POCSAG_QueueHead = (POCSAG_QueueHead + 1u) & (POCSAG_QUEUE_SIZE - 1u);
    }

    POCSAG_QueueTail = next_tail;
}

/*===========================================================================*/
/*  Reset dekodéru                                                            */
/*===========================================================================*/

static void reset_decoder(void)
{
    state          = POCSAG_STATE_NOISE;
    sample_phase   = 0u;
    shift_reg      = 0u;
    last_bit       = 0xFFu;
    preamble_count = 0u;
    bit_count      = 0u;
    word_count     = 0u;
    current_word   = 0u;
    batch_seq      = 0u;
}

/*===========================================================================*/
/*  BCH(31,21) – výpočet syndromu                                            */
/*===========================================================================*/

/**
 * Vstup: 31-bitový BCH codeword (bity 31..1 původního 32-bit slova >> 1).
 * Výstup: 10-bitový syndrom (0 = bezchybné slovo).
 */
static uint32_t bch_syndrome(uint32_t cw31)
{
    uint32_t reg = 0u;
    for (int8_t i = 30; i >= 0; i--) {
        uint32_t b = (cw31 >> (uint8_t)i) & 1u;
        if (((reg >> 9u) & 1u) ^ b) {
            reg = ((reg << 1u) ^ BCH_GENERATOR) & 0x3FFu;
        } else {
            reg = (reg << 1u) & 0x3FFu;
        }
    }
    return reg;
}

/*===========================================================================*/
/*  Veřejná funkce BCH kontroly a opravy                                     */
/*===========================================================================*/

POCSAG_BCHResult_t POCSAG_CheckBCH(uint32_t word, uint32_t *fixed_out)
{
    *fixed_out = word;

    /* Sudá parita celého 32-bit slova */
    uint32_t tmp    = word;
    uint8_t  parity = 0u;
    while (tmp) { parity ^= (uint8_t)(tmp & 1u); tmp >>= 1u; }

    /* BCH syndrom z 31 vyšších bitů */
    uint32_t cw31 = word >> 1u;
    uint32_t synd = bch_syndrome(cw31);

    if (synd == 0u) {
        return (parity == 0u) ? POCSAG_BCH_OK : POCSAG_BCH_PARITY;
    }

    /* Hledáme 1-bitovou chybu v 31-bit codeword */
    for (uint8_t i = 0u; i < BCH_WORD_BITS; i++) {
        if (bch_syndrome(cw31 ^ (1u << i)) == 0u) {
            /* Chyba na bitu i codeword → bitu (i+1) ve 32-bit slově */
            uint32_t corrected = word ^ (1u << (i + 1u));
            /* Opravíme i paritní bit pokud je stále špatný */
            tmp = corrected;
            uint8_t cp = 0u;
            while (tmp) { cp ^= (uint8_t)(tmp & 1u); tmp >>= 1u; }
            if (cp != 0u) corrected ^= 1u;
            *fixed_out = corrected;
            return POCSAG_BCH_FIXED;
        }
    }

    return POCSAG_BCH_ERROR;
}

/*===========================================================================*/
/*  Zpracování fronty – volat z main loopu                                    */
/*===========================================================================*/

/**
 * Vyzvedne jeden batch z fronty a vypíše ho na UART1.
 * @return 1 pokud byl batch zpracován, 0 pokud fronta prázdná.
 */
uint8_t POCSAG_ProcessQueue(void)
{
    if (POCSAG_QUEUE_EMPTY()) return 0u;

    /* Přečteme batch z head slotu (kopie na zásobníku pro bezpečnost) */
    uint8_t      head = POCSAG_QueueHead;
    POCSAG_Batch_t b  = POCSAG_BatchQueue[head];   /* kopie */

    /* Posuneme head – slot je nyní volný */
    POCSAG_QueueHead = (POCSAG_QueueHead + 1u) & (POCSAG_QUEUE_SIZE - 1u);

    print_batch(&b);
    return 1u;
}

/*===========================================================================*/
/*  Dekódování textové zprávy                                                 */
/*===========================================================================*/

/**
 * Z MESSAGE slov batche složí bitový proud a dekóduje 7-bitové ASCII znaky.
 * POCSAG přenáší znaky LSB-first. Výsledek zapíše do out[].
 */
static void decode_message(const POCSAG_Batch_t *b, char *out, uint8_t out_size)
{
    uint32_t bit_buf = 0u;
    uint8_t  bits_in = 0u;
    uint8_t  idx     = 0u;

    for (uint8_t i = 0u; i < POCSAG_WORDS_PER_BATCH; i++) {
        const POCSAG_Word_t *w = &b->words[i];
        if (w->type != POCSAG_WORD_MESSAGE) continue;

        /* 20 datových bitů slova, MSB první (jak jsou uloženy v w->data) */
        for (int8_t bit = 19; bit >= 0; bit--) {
            bit_buf = (bit_buf << 1u) | ((w->data >> (uint8_t)bit) & 1u);
            bits_in++;

            if (bits_in == 7u) {
                /* Máme 7 bitů uložených MSB-first, ale POCSAG kóduje LSB-first
                   → musíme obrátit pořadí bitů                               */
                uint8_t ch = 0u;
                for (uint8_t k = 0u; k < 7u; k++) {
                    ch = (uint8_t)((ch << 1u) | (bit_buf & 1u));
                    bit_buf >>= 1u;
                }
                bits_in = 0u;

                if (ch == 0x00u) goto done;          /* konec zprávy          */
                if (ch >= 0x20u && ch < 0x7Fu) {     /* tisknutelný ASCII     */
                    if (idx < (uint8_t)(out_size - 1u)) {
                        out[idx++] = (char)ch;
                    }
                }
            }
        }
    }
done:
    out[idx] = '\0';
}

/*===========================================================================*/
/*  Výpis batche na UART1                                                     */
/*===========================================================================*/

static void print_batch(const POCSAG_Batch_t *b)
{
    char buf[80];
    char msg[80];

    sendStringUART1("\r\n");
    sendStringUART1("======================================\r\n");

    sprintf(buf, "POCSAG BATCH #%u\r\n", b->batch_num);
    sendStringUART1(buf);
    sendStringUART1("======================================\r\n");

    sprintf(buf, "Slova    : %u / %u\r\n", b->count, POCSAG_WORDS_PER_BATCH);
    sendStringUART1(buf);
    sprintf(buf, "Opraveno : %u   Chyby: %u\r\n", b->fixed, b->errors);
    sendStringUART1(buf);

    if (b->errors == 0u && b->fixed == 0u) {
        sendStringUART1("Integrita: OK (bez chyb)\r\n");
    } else if (b->errors == 0u) {
        sendStringUART1("Integrita: OPRAVENO\r\n");
    } else {
        sendStringUART1("Integrita: CHYBA (neopravitelna)\r\n");
    }

    /* Najdeme první adresní slovo pro výpis adresy */
    uint8_t  addr_found = 0u;
    uint32_t address    = 0u;
    uint8_t  function   = 0u;
    for (uint8_t i = 0u; i < b->count && i < POCSAG_WORDS_PER_BATCH; i++) {
        if (b->words[i].type == POCSAG_WORD_ADDRESS) {
            /* Adresa = 18 bitů z dat + 3 bity z čísla framu (výběr adresy) */
            address  = ((uint32_t)b->words[i].data << 3u) | b->words[i].frame;
            /* Funkce: bity 12..11 původního slova (po BCH opravě)
               – uložena jako spodní 2 bity po extrakci z fixed slova       */
            uint32_t fixed_tmp = 0u;
            POCSAG_CheckBCH(b->words[i].raw, &fixed_tmp);
            function    = (uint8_t)((fixed_tmp >> 11u) & 0x03u);
            addr_found  = 1u;
            break;
        }
    }

    if (addr_found) {
        sprintf(buf, "Adresa   : %lu  Funkce: %u\r\n",
                (unsigned long)address, function);
        sendStringUART1(buf);
    } else {
        sendStringUART1("Adresa   : (zadny adresni slot)\r\n");
    }

    /* Dekódování zprávy */
    decode_message(b, msg, sizeof(msg));
    if (msg[0] != '\0') {
        sendStringUART1("Zprava   : ");
        sendStringUART1(msg);
        sendStringUART1("\r\n");
    } else {
        sendStringUART1("Zprava   : (tonove volani / prazdna)\r\n");
    }

    /* Tabulka slov */
    sendStringUART1("--------------------------------------\r\n");
    sendStringUART1("Fr Sl Typ  Raw-data   BCH\r\n");
    sendStringUART1("--------------------------------------\r\n");

    for (uint8_t i = 0u; i < b->count && i < POCSAG_WORDS_PER_BATCH; i++) {
        const POCSAG_Word_t *w = &b->words[i];
        const char *ts, *bs;

        switch (w->type) {
        case POCSAG_WORD_ADDRESS: ts = "ADR "; break;
        case POCSAG_WORD_MESSAGE: ts = "MSG "; break;
        default:                  ts = "IDLE"; break;
        }
        switch (w->bch) {
        case POCSAG_BCH_OK:     bs = "OK     "; break;
        case POCSAG_BCH_FIXED:  bs = "OPRAVEN"; break;
        case POCSAG_BCH_ERROR:  bs = "CHYBA  "; break;
        default:                bs = "PARITA "; break;
        }

        sprintf(buf, " %u  %u  %s 0x%08lX %s\r\n",
                w->frame, w->slot, ts, (unsigned long)w->raw, bs);
        sendStringUART1(buf);
    }

    sendStringUART1("======================================\r\n");
}

/*===========================================================================*/
/*  Diagnostický výpis – volat 1× za sekundu z IsSecond bloku               */
/*===========================================================================*/

void POCSAG_PrintDiag(void)
{
    char buf[100];

    /* Snapshot čítačů (čteme jednou, nulujeme per-sekundové) */
    uint32_t sc  = diag_sample_calls;  diag_sample_calls = 0;
    uint32_t br  = diag_bits_read;     diag_bits_read    = 0;
    uint32_t ec  = diag_edge_calls;    diag_edge_calls   = 0;
    uint32_t sb  = diag_same_bits;     diag_same_bits    = 0;
    uint16_t pm  = diag_preamble_max;  /* neresetujeme – max za celou dobu  */

    sendStringUART1("--- POCSAG diag ---\r\n");
    sprintf(buf, "Stav     : %s\r\n", POCSAG_GetStateName());
    sendStringUART1(buf);
    sprintf(buf, "PA0 val  : %u\r\n", (unsigned)GPIO_PinInGet(RX_PORT, RX_PIN));
    sendStringUART1(buf);
    sprintf(buf, "SampleCalls/s : %lu  (ocekavano ~2400)\r\n", (unsigned long)sc);
    sendStringUART1(buf);
    sprintf(buf, "BitsRead/s    : %lu  (ocekavano ~1200)\r\n", (unsigned long)br);
    sendStringUART1(buf);
    sprintf(buf, "Hrany/s  : %lu\r\n", (unsigned long)ec);
    sendStringUART1(buf);
    sprintf(buf, "SameBits/s    : %lu  (sumu; 0=ok preamble)\r\n", (unsigned long)sb);
    sendStringUART1(buf);
    sprintf(buf, "PreAmMax : %u   (potreba >= %u)\r\n", pm, POCSAG_PREAMBLE_BITS);
    sendStringUART1(buf);
    sprintf(buf, "ShiftReg : 0x%08lX\r\n", (unsigned long)shift_reg);
    sendStringUART1(buf);
    sprintf(buf, "PreamCnt : %u\r\n", preamble_count);
    sendStringUART1(buf);
}

/*===========================================================================*/
/*  Název stavu dekodéru                                                      */
/*===========================================================================*/

const char *POCSAG_GetStateName(void)
{
    switch (state) {
    case POCSAG_STATE_NOISE:    return "NOISE";
    case POCSAG_STATE_PREAMBLE: return "PREAMBLE/SYNC";
    case POCSAG_STATE_DATA:     return "DATA";
    default:                    return "???";
    }
}
