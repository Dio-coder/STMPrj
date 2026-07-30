/*
 * Minimal blinky for STM32F103C8T6 (Blue Pill)
 *
 * Toggles the onboard LED on PC13.
 * No HAL, no libraries — just register operations.
 *
 * Blue Pill LED circuit: PC13 → LED → VCC
 *   PC13 LOW  = LED on  (current sinks through pin)
 *   PC13 HIGH = LED off
 */

#include "stm32f103_regs.h"
int cnt = 100;
static void delay(volatile uint32_t count)
{
    while (count--)
        ;
}

int main(void)
{
    /*
     * Step 1: Enable GPIOC clock
     *
     * On STM32, peripherals are disabled by default to save power.
     * Before using any peripheral, you must enable its clock in the
     * RCC (Reset and Clock Control) register.
     */
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;

    /*
     * Step 2: Configure PC13 as push-pull output, 2 MHz
     *
     * CRH controls pins 8-15. Each pin uses 4 bits:
     *   [1:0] = MODE (00=input, 01=10MHz, 10=2MHz, 11=50MHz)
     *   [3:2] = CNF  (output: 00=push-pull, 01=open-drain)
     *
     * Pin 13 is at bit position (13-8)*4 = 20
     *   bits [21:20] = MODE = 0b10 (2 MHz output)
     *   bits [23:22] = CNF  = 0b00 (push-pull)
     */
    GPIOC->CRH &= ~(0xFU << 20);                   /* Clear pin 13 config */
    GPIOC->CRH |= (GPIO_MODE_OUT_2MHZ << 20);       /* MODE = 2 MHz */
    /* CNF already 0 (push-pull) after clearing */

    /* Step 3: Blink loop */
    while (1) {
        cnt++;
        GPIOC->ODR ^= (1U << 13);    /* Toggle PC13 */
        delay(200000);
    }
}
