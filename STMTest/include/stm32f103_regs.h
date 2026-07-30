#ifndef STM32F103_REGS_H
#define STM32F103_REGS_H

#include <stdint.h>

/* ---- Peripheral base addresses (from datasheet memory map) ---- */

#define PERIPH_BASE       ((uint32_t)0x40000000)
#define APB2_BASE         (PERIPH_BASE + 0x10000)
#define AHB_BASE          (PERIPH_BASE + 0x20000)

/* ---- RCC (Reset and Clock Control) ---- */

#define RCC_BASE          (AHB_BASE + 0x1000)   /* 0x40021000 */

typedef struct {
    volatile uint32_t CR;         /* 0x00  Clock control */
    volatile uint32_t CFGR;       /* 0x04  Clock configuration */
    volatile uint32_t CIR;        /* 0x08  Clock interrupt */
    volatile uint32_t APB2RSTR;   /* 0x0C  APB2 peripheral reset */
    volatile uint32_t APB1RSTR;   /* 0x10  APB1 peripheral reset */
    volatile uint32_t AHBENR;     /* 0x14  AHB peripheral clock enable */
    volatile uint32_t APB2ENR;    /* 0x18  APB2 peripheral clock enable */
    volatile uint32_t APB1ENR;    /* 0x1C  APB1 peripheral clock enable */
    volatile uint32_t BDCR;       /* 0x20  Backup domain control */
    volatile uint32_t CSR;        /* 0x24  Control/status */
} RCC_TypeDef;

#define RCC  ((RCC_TypeDef *)RCC_BASE)

/* RCC_APB2ENR bits */
#define RCC_APB2ENR_IOPAEN   (1U << 2)   /* GPIOA clock enable */
#define RCC_APB2ENR_IOPBEN   (1U << 3)   /* GPIOB clock enable */
#define RCC_APB2ENR_IOPCEN   (1U << 4)   /* GPIOC clock enable */

/* ---- GPIO ---- */

#define GPIOA_BASE        (APB2_BASE + 0x0800)  /* 0x40010800 */
#define GPIOB_BASE        (APB2_BASE + 0x0C00)  /* 0x40010C00 */
#define GPIOC_BASE        (APB2_BASE + 0x1000)  /* 0x40011000 */

typedef struct {
    volatile uint32_t CRL;    /* 0x00  Port configuration low  (pin 0-7)  */
    volatile uint32_t CRH;    /* 0x04  Port configuration high (pin 8-15) */
    volatile uint32_t IDR;    /* 0x08  Port input data */
    volatile uint32_t ODR;    /* 0x0C  Port output data */
    volatile uint32_t BSRR;   /* 0x10  Port bit set/reset */
    volatile uint32_t BRR;    /* 0x14  Port bit reset */
    volatile uint32_t LCKR;   /* 0x18  Port configuration lock */
} GPIO_TypeDef;

#define GPIOA  ((GPIO_TypeDef *)GPIOA_BASE)
#define GPIOB  ((GPIO_TypeDef *)GPIOB_BASE)
#define GPIOC  ((GPIO_TypeDef *)GPIOC_BASE)

/* GPIO CRH/CRL mode bits */
#define GPIO_MODE_INPUT     0x0   /* 00: Input mode */
#define GPIO_MODE_OUT_10MHZ 0x1   /* 01: Output 10 MHz */
#define GPIO_MODE_OUT_2MHZ  0x2   /* 10: Output 2 MHz */
#define GPIO_MODE_OUT_50MHZ 0x3   /* 11: Output 50 MHz */

/* GPIO CRH/CRL cnf bits (output) */
#define GPIO_CNF_OUT_PP     0x0   /* 00: General purpose push-pull */
#define GPIO_CNF_OUT_OD     0x1   /* 01: General purpose open-drain */

#endif /* STM32F103_REGS_H */
