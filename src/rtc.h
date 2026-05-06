/******************************************************************************
 * @file rtc.h
 * @brief Obsluha RTCC procesoru EFM32GG11B
 *        Taktovano z LFRCO (32.768 kHz), BCD kalendarni rezim
 *****************************************************************************/
#ifndef RTC_H
#define RTC_H

#include <stdint.h>
#include <stdbool.h>

// Struktura pro datum a cas
typedef struct {
    uint8_t hour;    // 0-23
    uint8_t min;     // 0-59
    uint8_t sec;     // 0-59
    uint8_t day;     // 1-31
    uint8_t month;   // 1-12
    uint8_t year;    // 0-99 (posledni dve cislice roku)
} rtc_datetime_t;

void initRTC(void);
void set_rtc(const rtc_datetime_t *dt);
void get_rtc(rtc_datetime_t *dt);
void show_rtc(void);

#endif /* RTC_H */
