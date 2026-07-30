#ifndef STM32F1XX_HAL_CONF_H
#define STM32F1XX_HAL_CONF_H

/* ---- 启用的 HAL 模块（只开需要的，减少编译量） ---- */
#define HAL_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED       /* UART HAL 内部依赖 DMA 头文件 */
#define HAL_FLASH_MODULE_ENABLED     /* RCC HAL 内部依赖 Flash 头文件 */

/* ---- 外部振荡器配置 ---- */
#define HSE_VALUE         8000000U   /* 外部高速晶振 8MHz (蓝色药丸标配) */
#define HSE_STARTUP_TIMEOUT  100U
#define HSI_VALUE         8000000U   /* 内部高速振荡器 8MHz */
#define LSE_VALUE         32768U
#define LSE_STARTUP_TIMEOUT  5000U
#define LSI_VALUE         40000U

/* ---- SysTick / 预取 ---- */
#define VDD_VALUE         3300U      /* 3.3V */
#define TICK_INT_PRIORITY 15U
#define USE_RTOS          0U
#define PREFETCH_ENABLE   1U

/* ---- 各模块头文件包含 ---- */
#ifdef HAL_RCC_MODULE_ENABLED
  #include "stm32f1xx_hal_rcc.h"
#endif
#ifdef HAL_GPIO_MODULE_ENABLED
  #include "stm32f1xx_hal_gpio.h"
#endif
#ifdef HAL_DMA_MODULE_ENABLED
  #include "stm32f1xx_hal_dma.h"
#endif
#ifdef HAL_CORTEX_MODULE_ENABLED
  #include "stm32f1xx_hal_cortex.h"
#endif
#ifdef HAL_FLASH_MODULE_ENABLED
  #include "stm32f1xx_hal_flash.h"
#endif
#ifdef HAL_UART_MODULE_ENABLED
  #include "stm32f1xx_hal_uart.h"
#endif

/* ---- assert_param: HAL 参数检查宏 ---- */
#ifdef USE_FULL_ASSERT
  #define assert_param(expr) ((expr) ? (void)0 : assert_failed((uint8_t *)__FILE__, __LINE__))
  void assert_failed(uint8_t *file, uint32_t line);
#else
  #define assert_param(expr) ((void)0)   /* Release 模式：不检查，零开销 */
#endif

#endif /* STM32F1XX_HAL_CONF_H */
