/******************************************************************************
 * @file rtc.c
 * @brief Obsluha RTCC procesoru EFM32GG11B
 *        Taktovano z LFRCO (32.768 kHz), BCD kalendarni rezim
 *
 * RTCC TIME register (BCD):
 *   bity 27..24 = hodiny desitky
 *   bity 23..20 = hodiny jednotky
 *   bity 19..16 = minuty desitky
 *   bity 15..12 = minuty jednotky
 *   bity 11.. 8 = sekundy desitky
 *   bity  7.. 4 = sekundy jednotky
 *
 * RTCC DATE register (BCD):
 *   bity 27..24 = rok desitky
 *   bity 23..20 = rok jednotky
 *   bity 19..16 = mesic desitky
 *   bity 15..12 = mesic jednotky
 *   bity 11.. 8 = den desitky
 *   bity  7.. 4 = den jednotky
 *****************************************************************************/
#include <stdio.h>
#include "em_cmu.h"
#include "em_rtcc.h"
#include "rtc.h"
#include "uart1.h"
#include "usart0.h"

//------------------------------------------------------------------------------
// Inicializace RTCC
// Zdroj: LFRCO (32.768 kHz), prescaler /32768 → 1 Hz tick
//------------------------------------------------------------------------------
void initRTC(void)
{
    CMU_ClockEnable(cmuClock_HFLE, true);
    CMU_ClockSelectSet(cmuClock_LFE, cmuSelect_LFRCO);
    CMU_ClockEnable(cmuClock_RTCC, true);

    RTCC_Unlock();          // ← unlock před Init, bez Reset()

    RTCC_Init_TypeDef rtccInit = RTCC_INIT_DEFAULT;
    rtccInit.enable           = true;   // ← rovnou zapnout
    rtccInit.debugRun         = false;
    rtccInit.precntWrapOnCCV0 = false;
    rtccInit.cntWrapOnCCV1    = false;
    rtccInit.prescMode        = rtccCntTickPresc;
    rtccInit.presc            = rtccCntPresc_32768;
    rtccInit.cntMode          = rtccCntModeCalendar;

    RTCC_Init(&rtccInit);
    // Nezamykat — Lock() blokuje i CTRL, nechame otevreny
}
//------------------------------------------------------------------------------
// Nastavi RTCC podle struktury rtc_datetime_t
// Volano z USART0_RX_IRQHandler() po prijeti NMEA vety s UTC
//------------------------------------------------------------------------------
void set_rtc(const rtc_datetime_t *dt)
{
    if (dt == NULL) return;

    char buf[40];
    sprintf(buf, "\r\nSET RTC by GPS: 20%02u-%02u-%02u  %02u:%02u:%02u\r\n",
            dt->year, dt->month, dt->day,
            dt->hour, dt->min, dt->sec);
    sendStringUART1(buf);

    // TIME: jednotky a desítky se zapisují jako samostatné nibble
    uint32_t timeReg =
        ((uint32_t)(dt->sec  % 10) <<  0) |   // SECU
        ((uint32_t)(dt->sec  / 10) <<  4) |   // SECT
        ((uint32_t)(dt->min  % 10) <<  8) |   // MINU
        ((uint32_t)(dt->min  / 10) << 12) |   // MINT
        ((uint32_t)(dt->hour % 10) << 16) |   // HOURU
        ((uint32_t)(dt->hour / 10) << 20);    // HOURT

    // DATE: stejný princip
    uint32_t dateReg =
        ((uint32_t)(dt->day   % 10) <<  0) |  // DAYU
        ((uint32_t)(dt->day   / 10) <<  4) |  // DAYT
        ((uint32_t)(dt->month % 10) <<  8) |  // MONTHU
        ((uint32_t)(dt->month / 10) << 12) |  // MONTHT
        ((uint32_t)(dt->year  % 10) << 16) |  // YEARU
        ((uint32_t)(dt->year  / 10) << 20);   // YEART

    RTCC_Unlock();
	RTCC_Enable(false);
	RTCC_TimeSet(timeReg);
	RTCC_DateSet(dateReg);
	RTCC_Enable(true);    // ← spustit PŘED Lock()
	RTCC_Lock();

    USART0_irq_disabled(); // Po nastaveni zakaze prijem znaku od GPS
}

//------------------------------------------------------------------------------
// Precte aktualni cas a datum z RTCC do struktury
//------------------------------------------------------------------------------
void get_rtc(rtc_datetime_t *dt)
{
    if (dt == NULL) return;

    uint32_t t = RTCC_TimeGet();
    uint32_t d = RTCC_DateGet();

    dt->sec   = ((t >>  4) & 0x7) * 10 + ((t >>  0) & 0xF);
    dt->min   = ((t >> 12) & 0x7) * 10 + ((t >>  8) & 0xF);
    dt->hour  = ((t >> 20) & 0x3) * 10 + ((t >> 16) & 0xF);

    dt->day   = ((d >>  4) & 0x3) * 10 + ((d >>  0) & 0xF);
    dt->month = ((d >> 12) & 0x1) * 10 + ((d >>  8) & 0xF);
    dt->year  = ((d >> 20) & 0xF) * 10 + ((d >> 16) & 0xF);
}

//------------------------------------------------------------------------------
// Vypise datum a cas na UART1
//------------------------------------------------------------------------------
void show_rtc(void)
{
    rtc_datetime_t dt;
    char buf[40];

    get_rtc(&dt);
    sprintf(buf, "UTC: 20%02u-%02u-%02u  %02u:%02u:%02u\r\n",
            dt.year, dt.month, dt.day,
            dt.hour, dt.min,   dt.sec);
    sendStringUART1(buf);
}
