/******************************************************************************
 * @file usart0.c COM-C
 * @brief Obsluha USART0 - 38400 8N1, LOCATION 0 (PE10/PE11)
 *        GPS NEO-M9N-00B (D_SEL=VCC → UART aktivní)
 *        Přijímá NMEA věty, kopíruje je na UART1.
 *        Zprávy obsahující UTC jsou označeny prefixem [UTC].
 *****************************************************************************/
#include <stddef.h>
#include "usart0.h"
#include "uart1.h"
#include "ports.h"
#include "em_usart.h"
#include "em_cmu.h"
#include "em_gpio.h"
#include "rtc.h"
#include "parameters.h"


char     rxBuffer1[BUFFER_SIZE];
volatile uint16_t rxIndex1 = 0;

// Poslední přijatá NMEA věta (pro main loop nebo další zpracování)
char     gps_sentence[BUFFER_SIZE];
volatile bool gps_sentence_ready = false;

void USART_BaudrateSet_Manual(USART_TypeDef *usart, uint32_t baudrate, uint32_t freq)
{
    uint32_t oversample = 16;
    uint32_t clkdiv = (((freq * 4) / (baudrate * oversample)) - 4) << 6;
    usart->CLKDIV = clkdiv;
}

void initUSART0(void)
{
    CMU_ClockEnable(cmuClock_USART0, true);
    CMU_ClockEnable(cmuClock_GPIO,   true);

    GPIO_PinModeSet(gpioPortE, 10, gpioModePushPull, 1); /* TX */
    GPIO_PinModeSet(gpioPortE, 11, gpioModeInput,    0); /* RX */

    USART0->CMD = USART_CMD_RXDIS | USART_CMD_TXDIS | USART_CMD_MASTERDIS
                | USART_CMD_RXBLOCKDIS | USART_CMD_TXTRIDIS
                | USART_CMD_CLEARTX | USART_CMD_CLEARRX;

    USART0->CTRL  = USART_CTRL_OVS_X16;
    USART0->FRAME = USART_FRAME_DATABITS_EIGHT
                  | USART_FRAME_PARITY_NONE
                  | USART_FRAME_STOPBITS_ONE;

    USART_BaudrateSet_Manual(USART0, 38400, HFCLK_FREQ);  // NEO-M9N výchozí baudrate

    USART0->ROUTEPEN  = USART_ROUTEPEN_RXPEN | USART_ROUTEPEN_TXPEN;
    USART0->ROUTELOC0 = (0 << _USART_ROUTELOC0_TXLOC_SHIFT)
                      | (0 << _USART_ROUTELOC0_RXLOC_SHIFT);

    USART0->CMD = USART_CMD_RXEN | USART_CMD_TXEN;

    USART_IntClear(USART0, _USART_IF_MASK);
    USART_IntEnable(USART0, USART_IEN_RXDATAV);
    NVIC_ClearPendingIRQ(USART0_RX_IRQn);
    NVIC_EnableIRQ(USART0_RX_IRQn);
}

void sendStringUSART0(const char *str)
{
    while (*str) { USART_Tx(USART0, *str++); }
}

//------------------------------------------------------------------------------
// Pomocná funkce: obsahuje věta podřetězec?
// NMEA věty obsahující UTC: $GPRMC, $GNRMC, $GPGGA, $GNGGA, $GPZDA
//------------------------------------------------------------------------------
static bool nmea_has_utc(const char *sentence)
{
    // RMC: $xxRMC - obsahuje datum i čas UTC
    // GGA: $xxGGA - obsahuje čas UTC
    // ZDA: $xxZDA - přímo UTC datum a čas
    if (sentence[0] != '$') return false;

    // Porovnat typ zprávy (znaky 3..5)
    const char *type = sentence + 3;
    if (type[0]=='R' && type[1]=='M' && type[2]=='C') return true;
    if (type[0]=='G' && type[1]=='G' && type[2]=='A') return true;
    if (type[0]=='Z' && type[1]=='D' && type[2]=='A') return true;

    return false;
}

//------------------------------------------------------------------------------
// Povoleni / zakaz preruseni od USART0 - ovladatelne z main.c
//------------------------------------------------------------------------------
void USART0_irq_enabled(void)
{
    USART_IntClear(USART0, _USART_IF_MASK);
    NVIC_ClearPendingIRQ(USART0_RX_IRQn);
    NVIC_EnableIRQ(USART0_RX_IRQn);
}

void USART0_irq_disabled(void)
{
    NVIC_DisableIRQ(USART0_RX_IRQn);
}

//------------------------------------------------------------------------------
// Parsovani UTC casu z NMEA vety $xxRMC nebo $xxGGA
// RMC format: $GPRMC,HHMMSS.ss,A,lat,N,lon,E,...,DDMMYY,...
// GGA format: $GPGGA,HHMMSS.ss,...
//
// Vraci true pokud se podarilo naparsovat cas (a datum u RMC)
//------------------------------------------------------------------------------
static bool parse_nmea_utc(const char *sentence, rtc_datetime_t *dt)
{
    if (sentence == NULL || dt == NULL) return false;
    if (sentence[0] != '$') return false;

    // Najit zacatek prvniho datoveho pole (za prvni carkou)
    const char *p = sentence;
    while (*p && *p != ',') p++;
    if (*p == '\0') return false;
    p++; // preskocit carku

    // p nyní ukazuje na HHMMSS.ss
    if (p[0]<'0'||p[0]>'9') return false; // prazdne pole = nema fix

    dt->hour  = (p[0]-'0')*10 + (p[1]-'0');
    dt->min   = (p[2]-'0')*10 + (p[3]-'0');
    dt->sec   = (p[4]-'0')*10 + (p[5]-'0');

    // Datum je pouze v RMC — najit 9. pole (DDMMYY)
    const char *type = sentence + 3;
    if (type[0]=='R' && type[1]=='M' && type[2]=='C') {
        // Prejit na 9. pole (0=typ, 1=cas, 2=status, 3=lat, 4=N/S,
        //                    5=lon, 6=E/W, 7=rychlost, 8=kurz, 9=datum)
        const char *q = p;
        for (int field = 0; field < 8; field++) {
            while (*q && *q != ',') q++;
            if (*q == '\0') return false;
            q++;
        }
        // q nyni ukazuje na DDMMYY
        if (q[0]<'0'||q[0]>'9') return false;
        dt->day   = (q[0]-'0')*10 + (q[1]-'0');
        dt->month = (q[2]-'0')*10 + (q[3]-'0');
        dt->year  = (q[4]-'0')*10 + (q[5]-'0');
    }

    return true;
}

//------------------------------------------------------------------------------
// IRQ handler - příjem NMEA vět z GPS NEO-M9N
// NMEA věta začíná '$', končí '\n' (předchází '\r')
// Kompletní věta se kopíruje na UART1.
// Věty s UTC jsou označeny prefixem [UTC].
//------------------------------------------------------------------------------

void USART0_RX_IRQHandler(void)
{
    uint8_t data = USART_Rx(USART0);

    if (data == '$') {
        rxIndex1 = 0;
        rxBuffer1[rxIndex1++] = data;
    }
    else if (data == '\n') {
        if (rxIndex1 > 0 && rxIndex1 < BUFFER_SIZE - 2) {
            rxBuffer1[rxIndex1++] = '\n';
            rxBuffer1[rxIndex1]   = '\0';

            bool utc = nmea_has_utc(rxBuffer1);

            if (utc) {
            	if (show_gps_nmea==1) {sendStringUART1("[UTC] ");}

                const char *type = rxBuffer1 + 3;
                if (type[0]=='R' && type[1]=='M' && type[2]=='C') {
                    rtc_datetime_t dt = {0};
                    if (parse_nmea_utc(rxBuffer1, &dt)) {
                        set_rtc(&dt);
//                    sendStringUART1("RTC SETTINGS\r\n");
                    }
                }
            }

            if (show_gps_nmea==1) {sendStringUART1(rxBuffer1);}

            for (uint16_t i = 0; i <= rxIndex1; i++) {
                gps_sentence[i] = rxBuffer1[i];
            }
            gps_sentence_ready = true;
        }
        rxIndex1 = 0;
    }
    else if (data != '\r') {
        if (rxIndex1 < BUFFER_SIZE - 1) {
            rxBuffer1[rxIndex1++] = data;
        } else {
            rxIndex1 = 0;
        }
    }
}
