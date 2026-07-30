
/*
 * STM32F103C8T6 HAL 工程 — LED 闪烁 + UART 打印
 *
 * 和裸机项目对比：
 *   裸机：RCC->APB2ENR |= ...; GPIOC->CRH = ...;
 *   HAL ：HAL_RCC_ClockConfig(); HAL_GPIO_Init(); HAL_UART_Transmit();
 *
 * HAL 底层做的事和你在 STMTest 里手写的完全一样，只是封装了。
 */

#include "stm32f1xx_hal.h"
#include <string.h>

UART_HandleTypeDef huart1;

/* ---- 时钟配置：先用 HSI 8MHz 跑通，后续再切 HSE+PLL 72MHz ---- */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    /* 用内部 HSI 8MHz（一定可用，不依赖外部晶振） */
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState = RCC_PLL_NONE;
    HAL_RCC_OscConfig(&osc);

    /* 直接用 HSI 作为 SYSCLK，不经过 PLL */
    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                  | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_0);
}

/* ---- GPIO 初始化：PC13 LED ---- */
static void GPIO_Init(void)
{
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_13;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);
}

/* ---- UART1 初始化：PA9=TX, PA10=RX, 115200 ---- */
static void UART1_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();

    /* PA9 = USART1_TX (复用推挽) */
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_9;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* PA10 = USART1_RX (浮空输入) */
    gpio.Pin = GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* UART 参数 */
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    HAL_UART_Init(&huart1);
}

/* ---- 简单的打印封装 ---- */
static void uart_print(const char *s)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)s, strlen(s), HAL_MAX_DELAY);
}

/* ---- main ---- */
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    GPIO_Init();
    UART1_Init();

    uart_print("Hello from STM32 HAL!\r\n");

    int cnt = 0;
    char buf[32];

    while (1) {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);

        /* 手动 int → string（不依赖 printf/sprintf，省 Flash） */
        uart_print("blink ");
        int n = cnt++;
        int i = 0;
        char tmp[12];
        if (n == 0) { tmp[i++] = '0'; }
        else { while (n > 0) { tmp[i++] = '0' + (n % 10); n /= 10; } }
        /* reverse */
        for (int j = 0; j < i; j++) buf[j] = tmp[i - 1 - j];
        buf[i] = '\r'; buf[i+1] = '\n'; buf[i+2] = '\0';
        uart_print(buf);

        HAL_Delay(500);
    }
}

/* ---- HAL 需要的回调桩 ---- */

/* SysTick 中断处理：HAL_Delay() 依赖它来计时 */
void SysTick_Handler(void)
{
    HAL_IncTick();
}
