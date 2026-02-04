/***************************************************************************//**
 * @file main.c
 * @brief Program pro obsluhu tří RS232 portů a čtyř LED diod na EFM32GG11B820F2048GQ64
 * @version 3.4
 * @note Upraven pro QFP64 balíček s LED1 na PA8, LED2 na PD5, LED3 na PD6, LED4 na PD8
 * @note LED3 zobrazuje stav vstupu OnBattery (PA3)
 * @note LED4 zobrazuje stav vstupu Tamper (PA4)
 * @note TIMER0 (1 Hz) bliká LED2, TIMER1 (1200 Hz) toggleuje TX a posílá '*' na UART1
 ******************************************************************************/

#include "em_device.h"
#include "em_chip.h"
#include "em_cmu.h"
#include "em_gpio.h"
#include "em_usart.h"
#include "em_emu.h"

// Buffery pro příjem dat
#define BUFFER_SIZE 256
char rxBuffer1[BUFFER_SIZE];
char rxBuffer2[BUFFER_SIZE];
char rxBuffer3[BUFFER_SIZE];
volatile uint16_t rxIndex1 = 0;
volatile uint16_t rxIndex2 = 0;
volatile uint16_t rxIndex3 = 0;

// Definice vystupnich portu
#define TX_PORT   (gpioPortA)
#define TX_PIN    (1)  // TX

#define PTT_PORT   (gpioPortA)
#define PTT_PIN    (2)  // PTT

// Definice vstupních portů
#define ONBATTERY_PORT  (gpioPortA)
#define ONBATTERY_PIN   (3)  // PA3

#define TAMPER_PORT     (gpioPortA)
#define TAMPER_PIN      (4)  // PA4

// Definice pro LED diody
#define LED1_PORT          (gpioPortA)
#define LED1_PIN           (8)  // PA8 (Uživatelská LED)

#define LED2_PORT          (gpioPortD)
#define LED2_PIN           (5)  // PD5 (Toggle 1Hz přes TIMER0)

#define LED3_PORT          (gpioPortD)
#define LED3_PIN           (6)  // PD6 (Zobrazuje stav OnBattery)

#define LED4_PORT          (gpioPortD)
#define LED4_PIN           (8)  // PD8 (Zobrazuje stav Tamper)

// Konstanta frekvence HFXO
#define HFXO_FREQ  24000000UL

/***************************************************************************//**
 * @brief Inicializace hodin procesoru s externím krystalem 24 MHz
 * @note Zapne HFXO (High Frequency Crystal Oscillator) a přepne na něj HFCLK
 * @note DŮLEŽITÉ: Musí se volat PŘED inicializací UART!
 ******************************************************************************/
void initClocks(void)
{
    // Inicializace struktury pro HFXO
    CMU_HFXOInit_TypeDef hfxoInit = CMU_HFXOINIT_DEFAULT;

    // Konfigurace pro krystalový režim
    hfxoInit.mode = cmuOscMode_Crystal;

    // Inicializace HFXO s krystalem 24 MHz
    CMU_HFXOInit(&hfxoInit);

    // Zapnout HFXO a počkat na stabilizaci
    CMU_OscillatorEnable(cmuOsc_HFXO, true, true);

    // Přepnout HFCLK z HFRCO na HFXO
    CMU_ClockSelectSet(cmuClock_HF, cmuSelect_HFXO);

    // Nastavit HFPERCLK dělič na 1 (periférie běží na plných 24 MHz)
    // PRESC = 0 znamená dělič 1 (HFPERCLK = HFCLK)
    CMU->HFPERPRESC = (CMU->HFPERPRESC & ~_CMU_HFPERPRESC_PRESC_MASK);

    // Povolit hodiny pro periferní sběrnici
    CMU_ClockEnable(cmuClock_HFPER, true);
}

/***************************************************************************//**
 * @brief Inicializace vstupních portů
 ******************************************************************************/
void initInputs(void)
{
    // Povolení hodin pro GPIO (pokud ještě není)
    CMU_ClockEnable(cmuClock_GPIO, true);

    // Konfigurace PA3 (OnBattery) jako vstup s pull-down
    GPIO_PinModeSet(ONBATTERY_PORT, ONBATTERY_PIN, gpioModeInputPullFilter, 0);

    // Konfigurace PA4 (Tamper) jako vstup s pull-down
    GPIO_PinModeSet(TAMPER_PORT, TAMPER_PIN, gpioModeInputPullFilter, 0);
}

/***************************************************************************//**
 * @brief Inicializace všech výstupů
 ******************************************************************************/
void initOutputs(void)
{
    // Povolení hodin pro GPIO (pokud ještě není)
    CMU_ClockEnable(cmuClock_GPIO, true);

    // Konfigurace GPIO pro TX port (PA1)
    GPIO_PinModeSet(TX_PORT, TX_PIN, gpioModePushPull, 0);

    // Konfigurace GPIO pro PTT port (PA2)
    GPIO_PinModeSet(PTT_PORT, PTT_PIN, gpioModePushPull, 0);

    // Konfigurace LED1 - PA8 jako výstup, výchozí stav LOW
    GPIO_PinModeSet(LED1_PORT, LED1_PIN, gpioModePushPull, 0);

    // Konfigurace LED2 - PD5 jako výstup, výchozí stav LOW
    GPIO_PinModeSet(LED2_PORT, LED2_PIN, gpioModePushPull, 0);

    // Konfigurace LED3 - PD6 jako výstup, výchozí stav LOW
    GPIO_PinModeSet(LED3_PORT, LED3_PIN, gpioModePushPull, 0);

    // Konfigurace LED4 - PD8 jako výstup, výchozí stav LOW
    GPIO_PinModeSet(LED4_PORT, LED4_PIN, gpioModePushPull, 0);
}

/***************************************************************************//**
 * @brief Rozsvícení LED1
 ******************************************************************************/
void LED1_On(void)
{
    GPIO_PinOutSet(LED1_PORT, LED1_PIN);
}

/***************************************************************************//**
 * @brief Zhasnutí LED1
 ******************************************************************************/
void LED1_Off(void)
{
    GPIO_PinOutClear(LED1_PORT, LED1_PIN);
}

/***************************************************************************//**
 * @brief Přepnutí stavu LED1
 ******************************************************************************/
void LED1_Toggle(void)
{
    GPIO_PinOutToggle(LED1_PORT, LED1_PIN);
}

/***************************************************************************//**
 * @brief Přepnutí stavu LED2
 ******************************************************************************/
void LED2_Toggle(void)
{
    GPIO_PinOutToggle(LED2_PORT, LED2_PIN);
}

/***************************************************************************//**
 * @brief Nastavení LED3 podle stavu vstupu OnBattery
 ******************************************************************************/
void LED3_SetFromInput(void)
{
    if (GPIO_PinInGet(ONBATTERY_PORT, ONBATTERY_PIN)) {
        GPIO_PinOutSet(LED3_PORT, LED3_PIN);
    } else {
        GPIO_PinOutClear(LED3_PORT, LED3_PIN);
    }
}

/***************************************************************************//**
 * @brief Nastavení LED4 podle stavu vstupu Tamper
 ******************************************************************************/
void LED4_SetFromInput(void)
{
    if (GPIO_PinInGet(TAMPER_PORT, TAMPER_PIN)) {
        GPIO_PinOutSet(LED4_PORT, LED4_PIN);
    } else {
        GPIO_PinOutClear(LED4_PORT, LED4_PIN);
    }
}

/***************************************************************************//**
 * @brief Inicializace TIMER0 pro generování přerušení 1Hz (jednou za vteřinu)
 * @note Používá se pro toggle LED2
 * @note HFXO = 24 MHz, Předděličkaa 1024 → 24000000 / 1024 = 23437.5 Hz
 *       Pro 1 Hz: 23437.5 / 1 ≈ 23437
 ******************************************************************************/
void initTIMER0(void)
{
    // Povolení hodin pro TIMER0
    CMU_ClockEnable(cmuClock_TIMER0, true);

    // Reset časovače
    TIMER0->CTRL = 0;
    TIMER0->CNT = 0;

    // Výpočet TOP hodnoty pro 1 Hz
    uint32_t topValue = 23437 - 1;

    // Nastavení TOP hodnoty
    TIMER0->TOP = topValue;

    // Konfigurace CTRL registru
    // PRESC = 1024 (hodnota 10 v bitu PRESC), MODE = Up counting
    TIMER0->CTRL = (10 << _TIMER_CTRL_PRESC_SHIFT) | TIMER_CTRL_MODE_UP;

    // Vymazání všech příznaků přerušení
    TIMER0->IFC = _TIMER_IFC_MASK;

    // Povolení přerušení overflow
    TIMER0->IEN = TIMER_IEN_OF;

    // Povolení přerušení v NVIC
    NVIC_ClearPendingIRQ(TIMER0_IRQn);
    NVIC_EnableIRQ(TIMER0_IRQn);

    // Spuštění časovače
    TIMER0->CMD = TIMER_CMD_START;
}

/***************************************************************************//**
 * @brief Inicializace TIMER1 pro generování přerušení 1200Hz
 * @note Používá se pro toggle TX portu a odesílání '*' na UART1
 * @note HFXO = 24 MHz, Předděličkaa 16 → 24000000 / 16 = 1500000 Hz
 *       Pro 1200 Hz: 1500000 / 1200 = 1250
 ******************************************************************************/
void initTIMER1(void)
{
    // Povolení hodin pro TIMER1
    CMU_ClockEnable(cmuClock_TIMER1, true);

    // Reset časovače
    TIMER1->CTRL = 0;
    TIMER1->CNT = 0;

    // Výpočet TOP hodnoty pro 1200 Hz
    uint32_t topValue = 1250 - 1;

    // Nastavení TOP hodnoty
    TIMER1->TOP = topValue;

    // Konfigurace CTRL registru
    // PRESC = 16 (hodnota 4 v bitu PRESC), MODE = Up counting
    TIMER1->CTRL = (4 << _TIMER_CTRL_PRESC_SHIFT) | TIMER_CTRL_MODE_UP;

    // Vymazání všech příznaků přerušení
    TIMER1->IFC = _TIMER_IFC_MASK;

    // Povolení přerušení overflow
    TIMER1->IEN = TIMER_IEN_OF;

    // Povolení přerušení v NVIC
    NVIC_ClearPendingIRQ(TIMER1_IRQn);
    NVIC_EnableIRQ(TIMER1_IRQn);

    // Spuštění časovače
    TIMER1->CMD = TIMER_CMD_START;
}

/***************************************************************************//**
 * @brief Obsluha přerušení TIMER0 - 1Hz
 ******************************************************************************/
void TIMER0_IRQHandler(void)
{
    // Vymazání příznaku přerušení overflow
    TIMER0->IFC = TIMER_IFC_OF;

    // Toggle LED2
    LED2_Toggle();
}

/***************************************************************************//**
 * @brief Obsluha přerušení TIMER1 - 1200Hz
 * @note Pouze toggle TX portu
 ******************************************************************************/
void TIMER1_IRQHandler(void)
{
    // Vymazání příznaku přerušení overflow
    TIMER1->IFC = TIMER_IFC_OF;

    // Toggle TX port (pro měření frekvence osciloskopem)
    GPIO_PinOutToggle(TX_PORT, TX_PIN);
}

/***************************************************************************//**
 * @brief Manuální nastavení baudrate pro USART
 * @param usart Ukazatel na USART periférii (USART0, UART0, UART1)
 * @param baudrate Požadovaný baudrate
 * @param freq Frekvence hodin periférie (HFPERCLK)
 ******************************************************************************/
void USART_BaudrateSet_Manual(USART_TypeDef *usart, uint32_t baudrate, uint32_t freq)
{
    uint32_t oversample = 16; // Standardní oversample pro async režim
    uint32_t clkdiv;

    // Výpočet CLKDIV: (freq / (oversample * baudrate) - 1) << 6
    // Bit shift o 6 je kvůli formátu registru CLKDIV (fractional part)
    clkdiv = (((freq * 4) / (baudrate * oversample)) - 4) << 6;

    // Nastavení CLKDIV registru
    usart->CLKDIV = clkdiv;
}

/***************************************************************************//**
 * @brief Inicializace USART0 - první UART (115200, 8N1)
 * @note Pro QFN64: Použita LOCATION 0 (PE10/PE11)
 * @note MUSÍ se volat AŽ PO initClocks()!
 ******************************************************************************/
void initUSART0(void)
{
    // Povolení hodin pro USART0 a GPIO
    CMU_ClockEnable(cmuClock_USART0, true);
    CMU_ClockEnable(cmuClock_GPIO, true);

    // Konfigurace GPIO pinů pro USART0 LOCATION 0
    // TX - PE10
    GPIO_PinModeSet(gpioPortE, 10, gpioModePushPull, 1);
    // RX - PE11
    GPIO_PinModeSet(gpioPortE, 11, gpioModeInput, 0);

    // Reset USART0
    USART0->CMD = USART_CMD_RXDIS | USART_CMD_TXDIS | USART_CMD_MASTERDIS |
                   USART_CMD_RXBLOCKDIS | USART_CMD_TXTRIDIS | USART_CMD_CLEARTX | USART_CMD_CLEARRX;

    // Základní konfigurace USART0
    USART0->CTRL = USART_CTRL_OVS_X16;  // Oversample 16
    USART0->FRAME = USART_FRAME_DATABITS_EIGHT |
                     USART_FRAME_PARITY_NONE |
                     USART_FRAME_STOPBITS_ONE;

    // Manuální nastavení baudrate pro 115200 @ 24 MHz
    USART_BaudrateSet_Manual(USART0, 115200, HFXO_FREQ);

    // Nastavení lokace pinů (LOCATION 0)
    USART0->ROUTEPEN = USART_ROUTEPEN_RXPEN | USART_ROUTEPEN_TXPEN;
    USART0->ROUTELOC0 = (USART0->ROUTELOC0 & ~(_USART_ROUTELOC0_TXLOC_MASK |
                                                _USART_ROUTELOC0_RXLOC_MASK)) |
                        (0 << _USART_ROUTELOC0_TXLOC_SHIFT) |
                        (0 << _USART_ROUTELOC0_RXLOC_SHIFT);

    // Zapnutí TX a RX
    USART0->CMD = USART_CMD_RXEN | USART_CMD_TXEN;

    // Povolení přerušení pro příjem
    USART_IntClear(USART0, _USART_IF_MASK);
    USART_IntEnable(USART0, USART_IEN_RXDATAV);
    NVIC_ClearPendingIRQ(USART0_RX_IRQn);
    NVIC_EnableIRQ(USART0_RX_IRQn);
}

/***************************************************************************//**
 * @brief Inicializace UART0 - druhý UART (9600, 8N1)
 * @note Pro QFN64: Použita LOCATION 4 (PC4/PC5)
 * @note MUSÍ se volat AŽ PO initClocks()!
 ******************************************************************************/
void initUART0(void)
{
    // Povolení hodin pro UART0
    CMU_ClockEnable(cmuClock_UART0, true);

    // Konfigurace GPIO pinů pro UART0 LOCATION 4
    // TX - PC4
    GPIO_PinModeSet(gpioPortC, 4, gpioModePushPull, 1);
    // RX - PC5
    GPIO_PinModeSet(gpioPortC, 5, gpioModeInput, 0);

    // Reset UART0
    UART0->CMD = USART_CMD_RXDIS | USART_CMD_TXDIS | USART_CMD_MASTERDIS |
                  USART_CMD_RXBLOCKDIS | USART_CMD_TXTRIDIS | USART_CMD_CLEARTX | USART_CMD_CLEARRX;

    // Základní konfigurace UART0
    UART0->CTRL = USART_CTRL_OVS_X16;  // Oversample 16
    UART0->FRAME = USART_FRAME_DATABITS_EIGHT |
                    USART_FRAME_PARITY_NONE |
                    USART_FRAME_STOPBITS_ONE;

    // Manuální nastavení baudrate pro 9600 @ 24 MHz
    USART_BaudrateSet_Manual(UART0, 9600, HFXO_FREQ);

    // Nastavení lokace pinů (LOCATION 4)
    UART0->ROUTEPEN = USART_ROUTEPEN_RXPEN | USART_ROUTEPEN_TXPEN;
    UART0->ROUTELOC0 = (UART0->ROUTELOC0 & ~(_USART_ROUTELOC0_TXLOC_MASK |
                                              _USART_ROUTELOC0_RXLOC_MASK)) |
                       (4 << _USART_ROUTELOC0_TXLOC_SHIFT) |
                       (4 << _USART_ROUTELOC0_RXLOC_SHIFT);

    // Zapnutí TX a RX
    UART0->CMD = USART_CMD_RXEN | USART_CMD_TXEN;

    // Povolení přerušení pro příjem
    USART_IntClear(UART0, _USART_IF_MASK);
    USART_IntEnable(UART0, USART_IEN_RXDATAV);
    NVIC_ClearPendingIRQ(UART0_RX_IRQn);
    NVIC_EnableIRQ(UART0_RX_IRQn);
}

/***************************************************************************//**
 * @brief Inicializace UART1 - třetí UART (115200, 8N1)
 * @note Pro QFN64: Použita LOCATION 4 (PE12/PE13)
 * @note MUSÍ se volat AŽ PO initClocks()!
 ******************************************************************************/
void initUART1(void)
{
    // Povolení hodin pro UART1
    CMU_ClockEnable(cmuClock_UART1, true);

    // Konfigurace GPIO pinů pro UART1 LOCATION 4
    // TX - PE12
    GPIO_PinModeSet(gpioPortE, 12, gpioModePushPull, 1);
    // RX - PE13
    GPIO_PinModeSet(gpioPortE, 13, gpioModeInput, 0);

    // Reset UART1
    UART1->CMD = USART_CMD_RXDIS | USART_CMD_TXDIS | USART_CMD_MASTERDIS |
                  USART_CMD_RXBLOCKDIS | USART_CMD_TXTRIDIS | USART_CMD_CLEARTX | USART_CMD_CLEARRX;

    // Základní konfigurace UART1
    UART1->CTRL = USART_CTRL_OVS_X16;  // Oversample 16
    UART1->FRAME = USART_FRAME_DATABITS_EIGHT |
                    USART_FRAME_PARITY_NONE |
                    USART_FRAME_STOPBITS_ONE;

    // Manuální nastavení baudrate pro 115200 @ 24 MHz
    USART_BaudrateSet_Manual(UART1, 115200, HFXO_FREQ);

    // Nastavení lokace pinů (LOCATION 4)
    UART1->ROUTEPEN = USART_ROUTEPEN_RXPEN | USART_ROUTEPEN_TXPEN;
    UART1->ROUTELOC0 = (UART1->ROUTELOC0 & ~(_USART_ROUTELOC0_TXLOC_MASK |
                                              _USART_ROUTELOC0_RXLOC_MASK)) |
                       (4 << _USART_ROUTELOC0_TXLOC_SHIFT) |
                       (4 << _USART_ROUTELOC0_RXLOC_SHIFT);

    // Zapnutí TX a RX
    UART1->CMD = USART_CMD_RXEN | USART_CMD_TXEN;

    // Povolení přerušení pro příjem
    USART_IntClear(UART1, _USART_IF_MASK);
    USART_IntEnable(UART1, USART_IEN_RXDATAV);
    NVIC_ClearPendingIRQ(UART1_RX_IRQn);
    NVIC_EnableIRQ(UART1_RX_IRQn);
}

/***************************************************************************//**
 * @brief Odeslání řetězce přes USART0
 ******************************************************************************/
void sendStringUSART0(const char *str)
{
    while (*str) {
        USART_Tx(USART0, *str++);
    }
}

/***************************************************************************//**
 * @brief Odeslání řetězce přes UART0
 ******************************************************************************/
void sendStringUART0(const char *str)
{
    while (*str) {
        USART_Tx(UART0, *str++);
    }
}

/***************************************************************************//**
 * @brief Odeslání řetězce přes UART1
 ******************************************************************************/
void sendStringUART1(const char *str)
{
    while (*str) {
        USART_Tx(UART1, *str++);
    }
}

/***************************************************************************//**
 * @brief Obsluha přerušení USART0 RX
 ******************************************************************************/
void USART0_RX_IRQHandler(void)
{
    uint8_t data = USART_Rx(USART0);

    if (data == 13) { // CR
        rxBuffer1[rxIndex1] = '\0';
        sendStringUSART0(rxBuffer1);
        sendStringUSART0("\r\n");
        rxIndex1 = 0;
    } else {
        if (rxIndex1 < BUFFER_SIZE - 1) {
            rxBuffer1[rxIndex1++] = data;
        }
    }
}

/***************************************************************************//**
 * @brief Obsluha přerušení UART0 RX
 ******************************************************************************/
void UART0_RX_IRQHandler(void)
{
    uint8_t data = USART_Rx(UART0);

    if (data == 13) { // CR
        rxBuffer2[rxIndex2] = '\0';
        sendStringUART0(rxBuffer2);
        sendStringUART0("\r\n");
        rxIndex2 = 0;
    } else {
        if (rxIndex2 < BUFFER_SIZE - 1) {
            rxBuffer2[rxIndex2++] = data;
        }
    }
}

/***************************************************************************//**
 * @brief Obsluha přerušení UART1 RX
 ******************************************************************************/
void UART1_RX_IRQHandler(void)
{
    uint8_t data = USART_Rx(UART1);

    if (data == 13) { // CR
        rxBuffer3[rxIndex3] = '\0';
        sendStringUART1(rxBuffer3);
        sendStringUART1("\r\n");
        rxIndex3 = 0;
    } else {
        if (rxIndex3 < BUFFER_SIZE - 1) {
            rxBuffer3[rxIndex3++] = data;
        }
    }
}

/**************************************************************************//**
 * @brief Jednoduchá blokující funkce pro zpoždění
 *****************************************************************************/
void delay_ms(uint32_t ms)
{
    // Přepočítáno pro 24 MHz
    uint32_t cycles_per_ms = 858;

    volatile uint32_t i;
    uint32_t total_cycles = cycles_per_ms * ms;

    for (i = 0; i < total_cycles; i++)
    {
        i = i;
    }
}

/***************************************************************************//**
 * @brief Hlavní funkce
 ******************************************************************************/
int main(void)
{
    // Inicializace čipu
    CHIP_Init();

    // DŮLEŽITÉ: Inicializace hodin MUSÍ BÝT PRVNÍ!
    initClocks();

    // Inicializace vstupních portů
    initInputs();

    // Inicializace všech výstupních portů
    initOutputs();

    // Inicializace všech tří UART portů
    // MUSÍ BÝT AŽ PO initClocks()!
    initUSART0();
    initUART0();
    initUART1();

    // Inicializace časovačů
    initTIMER0();  // 1 Hz pro LED2
    initTIMER1();  // 1200 Hz pro TX a UART1

    // Krátké zpoždění pro stabilizaci
    for (volatile int i = 0; i < 100000; i++);

    // Odeslání hlaviček
    sendStringUART0 ("TCI COM1-A (UART0) - QFN64\r\n");
    sendStringUART1 ("TCI COM2-B (UART1) - QFN64 DEBUG1\r\n");
    sendStringUSART0("TCI COM3-C (USART0) - QFN64\r\n");

    sendStringUART1 ("HFXO: Externi krystal 24 MHz\r\n");
    sendStringUART1 ("LED2 blikani 1Hz (TIMER0)\r\n");
    sendStringUART1 ("LED3 = stav OnBattery (PA3)\r\n");
    sendStringUART1 ("LED4 = stav Tamper (PA4)\r\n");
    sendStringUART1 ("TX port (PA1) toggle 1200Hz\r\n");
    sendStringUART1 ("UART1 vypisuje '*' 1200Hz\r\n");

    // Hlavní smyčka
    while (1) {
        // Blikání LED1 s intervalem 200ms
//        LED1_On();
        GPIO_PinOutSet(LED1_PORT, LED1_PIN);
//        GPIO_PinOutSet(LED3_PORT, LED3_PIN);
//        GPIO_PinOutSet(LED4_PORT, LED4_PIN);
        GPIO_PinOutSet(PTT_PORT,  PTT_PIN);
        delay_ms(250);

//        LED1_Off();
        GPIO_PinOutClear(LED1_PORT, LED1_PIN);
//        GPIO_PinOutClear(LED3_PORT, LED3_PIN);
//        GPIO_PinOutClear(LED4_PORT, LED4_PIN);
        GPIO_PinOutClear(PTT_PORT,  PTT_PIN);
        delay_ms(250);

//        sendStringUART1 ("*");
        sendStringUART1 ("\r\n");

        // Aktualizace LED3 a LED4 podle stavu vstupů
//        LED3_SetFromInput();  // LED3 = OnBattery
//        LED4_SetFromInput();  // LED4 = Tamper


        if (GPIO_PinInGet(ONBATTERY_PORT, ONBATTERY_PIN)) {
            sendStringUART1 ("Batery:1  ");
            GPIO_PinOutSet(LED3_PORT, LED3_PIN);
        } else {
            sendStringUART1 ("Batery:0  ");
            GPIO_PinOutClear(LED3_PORT, LED3_PIN);
        }

        if (GPIO_PinInGet(TAMPER_PORT, TAMPER_PIN)) {
            sendStringUART1 ("Tamper:1  ");
            GPIO_PinOutSet(LED4_PORT, LED4_PIN);
        } else {
            sendStringUART1 ("Tamper:0  ");
            GPIO_PinOutClear(LED4_PORT, LED4_PIN);
        }

    }
}
