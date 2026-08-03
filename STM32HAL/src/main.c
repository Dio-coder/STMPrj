
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

/* ---- 时钟配置：HSE 8MHz × PLL9 = 72MHz，失败自动回退 HSI 8MHz ---- */

/* 实际生效的时钟源，供 main 打印 —— 一眼看出晶振到底能不能用 */
static const char *clock_source = "?";

/*
 * 切到 72MHz 有两件事必须同时改，漏任一个都出问题：
 *
 *   ① FLASH_LATENCY_2  —— F103 的 Flash 取指跟不上 CPU：
 *                          0~24MHz→0 个等待周期，24~48MHz→1，48~72MHz→2。
 *                          漏了会取指出错、间歇性跑飞，比 HardFault 难查得多。
 *   ② APB1CLKDivider=DIV2 —— APB1 总线上限 36MHz，72MHz 直连超规格，
 *                          挂在 APB1 上的 USART2/3、I2C、TIM2~7 会行为异常。
 *
 * 另外 HSI 路径到不了 72MHz：PLL 输入只能是 HSI/2(=4MHz) 或 HSE，
 * 而 PLLMUL 最大 ×16 → HSI 天花板 64MHz。所以 72MHz 必须用 HSE。
 */
static HAL_StatusTypeDef clock_config_hse_72m(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState       = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc.PLL.PLLState   = RCC_PLL_ON;
    osc.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL     = RCC_PLL_MUL9;         /* 8MHz × 9 = 72MHz */

    /* HSE 起不来时 HAL 内部等 HSE_STARTUP_TIMEOUT(100ms) 后返回 HAL_TIMEOUT，
       不会死等 —— 所以这里能安全地判返回值再决定回退。 */
    if (HAL_RCC_OscConfig(&osc) != HAL_OK)
        return HAL_ERROR;

    clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                       | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;      /* HCLK  = 72MHz */
    clk.APB1CLKDivider = RCC_HCLK_DIV2;        /* PCLK1 = 36MHz  ← ② */
    clk.APB2CLKDivider = RCC_HCLK_DIV1;        /* PCLK2 = 72MHz  (USART1 在这条) */

    return HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2);   /* ← ① */
}

/* 回退路径：片内 HSI 8MHz 直连，不依赖任何外部器件 */
static void clock_config_hsi_8m(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    osc.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState            = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState        = RCC_PLL_NONE;
    HAL_RCC_OscConfig(&osc);

    clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                       | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_HSI;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1;        /* 8MHz，远低于 36MHz 上限 */
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_0);
}

static void SystemClock_Config(void)
{
    if (clock_config_hse_72m() == HAL_OK) {
        clock_source = "HSE 8MHz x9 -> 72MHz";
    } else {
        clock_config_hsi_8m();
        clock_source = "HSI 8MHz (HSE FAILED!)";
    }
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

/* 手动 uint → 十进制字符串（不依赖 printf/sprintf，省 Flash） */
static void uart_print_uint(uint32_t v)
{
    char tmp[11], buf[12];
    int i = 0;

    if (v == 0) {
        tmp[i++] = '0';
    } else {
        while (v > 0) { tmp[i++] = (char)('0' + (v % 10)); v /= 10; }
    }
    for (int j = 0; j < i; j++) buf[j] = tmp[i - 1 - j];   /* 反转 */
    buf[i] = '\0';
    uart_print(buf);
}

/* ---- main ---- */
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    GPIO_Init();
    UART1_Init();

    uart_print("\r\nHello from STM32 HAL!\r\n");

    /* 打印实测时钟 —— 这就是「晶振到底能不能用」的判据 */
    uart_print("clock : ");
    uart_print(clock_source);
    uart_print("\r\nSYSCLK: ");
    uart_print_uint(SystemCoreClock);
    uart_print(" Hz\r\nPCLK1 : ");
    uart_print_uint(HAL_RCC_GetPCLK1Freq());
    uart_print(" Hz  (APB1, max 36MHz)\r\nPCLK2 : ");
    uart_print_uint(HAL_RCC_GetPCLK2Freq());
    uart_print(" Hz  (APB2, USART1)\r\n\r\n");

    uint32_t cnt = 0;
    while (1) {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        uart_print("blink ");
        uart_print_uint(cnt++);
        uart_print("\r\n");
        HAL_Delay(500);
    }
}

/* ---- HAL 需要的回调桩 ---- */

/* SysTick 中断处理：HAL_Delay() 依赖它来计时 */
void SysTick_Handler(void)
{
    HAL_IncTick();
}
