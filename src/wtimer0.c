/******************************************************************************
 * WTIMER0 (32 bitovy) běží na maximální možné frekvenci 72 MHz, aby mohl
 * změřit délku trvání několika bitu preamble s rozlišením 13,8 nanosekundy.
 * Neaktivuje preruseni, slouzi jen jako counter 72MHz stale dokola.
 * Z naměřené doby skutečné rychlosti pak přepočítáme TOP hodnotu pro TIMER1.
 *
 * 1200b/s je vzorkovano 72Mhz => jeden bit odpovida 60000 tikum čítače.
 *****************************************************************************/
#include "em_cmu.h"
#include "em_timer.h"
#include "wtimer0.h"

// Definice sdílených proměnných
volatile uint32_t last_edge_wtimer = 0;
volatile uint32_t measured_diff_wtimer = 0;
volatile bool new_edge_data = false;

/*
void initWTIMER0(void) {
    // 1. Povolení hodin pro WTIMER0
    CMU_ClockEnable(cmuClock_WTIMER0, true);

    // 2. Použití standardní struktury TIMER_Init_TypeDef
    TIMER_Init_TypeDef wtimerInit = TIMER_INIT_DEFAULT;

    // Nastavení prescaleru na 1 (72 MHz)
    wtimerInit.prescale = timerPrescale1;
    wtimerInit.enable   = true;

    TIMER_Init((TIMER_TypeDef *)WTIMER0, &wtimerInit);
}
*/

void initWTIMER0(void)
{
    //------------------------------------------------------------------
    // 1) Nastavit prescaler větve HFPERCCLK na 1 (= dělení 1 = 72 MHz)
    //    POZOR: tato větev sdílí clock pro WTIMER0-3.
    //    Pokud ostatní WTIMER timery nejsou použity, je to bezpečné.
    //------------------------------------------------------------------
    CMU_ClockPrescSet(cmuClock_HFPERC, 0);  // 0 = prescaler 1 (bez dělení)

    //------------------------------------------------------------------
    // 2) Povolit clock pro WTIMER0
    //------------------------------------------------------------------
    CMU_ClockEnable(cmuClock_WTIMER0, true);

    //------------------------------------------------------------------
    // 3) Inicializovat WTIMER0
    //    - PRESC = DIV1  → vstupní clock beze změny (72 MHz)
    //    - COUNT_UP, free-running (TOP = 0xFFFFFFFF)
    //------------------------------------------------------------------
    TIMER_Init_TypeDef timerInit = TIMER_INIT_DEFAULT;
    timerInit.enable    = true;
    timerInit.prescale  = timerPrescale1;   // interní dělička timeru = 1
    timerInit.clkSel    = timerClkSelHFPerClk;
    timerInit.count2x   = false;
    timerInit.debugRun  = false;

    TIMER_Init(WTIMER0, &timerInit);

    // Nastavit TOP na maximum (WTIMER je 32-bit)
    TIMER_TopSet(WTIMER0, 0xFFFFFFFFUL);
}

