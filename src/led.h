/***************************************************************************//**
 * Obsluha LED
 ******************************************************************************/
#ifndef LED_H
#define LED_H

void LED_Init(void);

void LED1_On(void); void LED1_Off(void); void LED1_Toggle(void);
void LED2_On(void); void LED2_Off(void); void LED2_Toggle(void);
void LED3_On(void); void LED3_Off(void); void LED3_Toggle(void);
void LED4_On(void); void LED4_Off(void); void LED4_Toggle(void);
void LED_RX_On(void); void LED_RX_Off(void); void LED_RX_Toggle(void);
void LED_TX_On(void); void LED_TX_Off(void); void LED_TX_Toggle(void);

#endif /* LED_H */
