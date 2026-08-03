# HAL 工程结构说明

每个文件为什么需要、谁提供、能不能改。

---

## 目录全貌

```
STMPrj/
├── STM32CubeF1/                    ← ST 官方 SDK（git clone 来的，不改）
│   └── Drivers/
│       ├── STM32F1xx_HAL_Driver/
│       │   ├── Inc/                ← HAL 头文件
│       │   └── Src/                ← HAL 实现（.c）
│       └── CMSIS/
│           ├── Include/            ← ARM 内核层（core_cm3.h 等）
│           └── Device/ST/STM32F1xx/
│               ├── Include/        ← 芯片寄存器定义（stm32f103xb.h）
│               └── Source/Templates/
│                   ├── system_stm32f1xx.c    ← SystemInit 实现
│                   └── gcc/startup_stm32f103xb.s  ← 官方启动文件
│
└── STM32HAL/                       ← 你的工程
    ├── src/
    │   ├── main.c                  ← 你的代码
    │   └── stm32f1xx_hal_conf.h    ← 你写的 HAL 配置（必需）
    ├── STM32F103C8.ld              ← 你写的链接脚本
    ├── Makefile                    ← 你写的构建脚本
    ├── compile_commands.json       ← bear 生成（给 VS Code 跳转）
    └── build/                      ← 编译产物
```

> **注意 HAL 库不在工程目录里**，通过 `Makefile` 里的 `CUBE = ../STM32CubeF1` 引用。
> 好处：多个工程共享一份 SDK，工程本身很干净。

---

## 三层结构：从上到下

```
┌─────────────────────────────────────┐
│ main.c                              │  你的应用代码
├─────────────────────────────────────┤
│ HAL 层  stm32f1xx_hal_gpio.c 等     │  ST 提供，封装寄存器操作
│ 配置：  stm32f1xx_hal_conf.h        │  ← 你写的，决定编译哪些模块
├─────────────────────────────────────┤
│ CMSIS 设备层  stm32f103xb.h         │  ST 提供，寄存器地址定义
│              system_stm32f1xx.c     │  ← 就是你裸机手写的那个头文件的完整版
├─────────────────────────────────────┤
│ CMSIS 内核层  core_cm3.h            │  ARM 提供，NVIC/SysTick/SCB 访问
├─────────────────────────────────────┤
│ 启动文件  startup_stm32f103xb.s     │  ST 提供，向量表 + Reset_Handler
└─────────────────────────────────────┘
```

### 和裸机项目的对应关系

| 裸机项目（STMTest） | HAL 项目 | 说明 |
|---|---|---|
| `include/stm32f103_regs.h`（手写 2 个外设） | `stm32f103xb.h`（官方，全部外设） | 同一件事，官方版完整得多 |
| `src/startup_stm32f103.s`（16 个向量） | `startup_stm32f103xb.s`（完整向量） | 官方版多了 `bl SystemInit` 和 `bl __libc_init_array` |
| 无 | `system_stm32f1xx.c` | 提供 `SystemInit()` 实现 |
| 无 | HAL 库 `.c` 文件 | 把寄存器操作封装成函数 |
| 无 | `stm32f1xx_hal_conf.h` | HAL 库的编译期配置 |
| `GPIOC->CRH \|= ...` | `HAL_GPIO_Init(GPIOC, &gpio)` | HAL 底层做的事完全一样，只是封装了 |

---

## 逐个文件说明

### `src/main.c` —— 你的代码

结构固定：
```c
int main(void)
{
    HAL_Init();              // 初始化 SysTick、Flash 预取、NVIC 优先级分组
    SystemClock_Config();    // 配置时钟树（HSI/HSE → PLL → SYSCLK → AHB/APB）
    GPIO_Init();             // 外设初始化
    UART1_Init();
    while (1) { ... }
}
```

**必须自己写的中断处理**：
```c
void SysTick_Handler(void)
{
    HAL_IncTick();   // 不写这个，HAL_Delay() 会永久阻塞
}
```
原理见 `DEBUG_NOTES.md` 里的 weak 符号说明。

### `src/stm32f1xx_hal_conf.h` —— HAL 配置（必需，不可省）

这个文件被 `stm32f1xx_hal.h` 无条件 include，**HAL 库编译的第一道门**。它干三件事：

**① 模块开关**——决定编译哪些 HAL 头文件
```c
#define HAL_GPIO_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED     /* UART HAL 内部依赖 DMA 头文件 */
#define HAL_FLASH_MODULE_ENABLED   /* RCC HAL 内部依赖 Flash 头文件 */
```
> **注意最后两个**：你不用 DMA 和 Flash，但 UART/RCC 的头文件里引用了它们的类型，不开就编译不过。这种隐式依赖只能靠报错试出来。

**② 硬件参数**——HAL 代码里算波特率、算延时都要用
```c
#define HSE_VALUE  8000000U   /* 外部晶振频率 */
#define HSI_VALUE  8000000U   /* 内部振荡器频率 */
#define VDD_VALUE  3300U
#define TICK_INT_PRIORITY 15U /* SysTick 中断优先级（数值越大越低） */
```

**③ `assert_param` 宏定义**——HAL 库源码里到处调用，不定义就链接报错
```c
#ifndef USE_FULL_ASSERT
  #define assert_param(expr) ((void)0)   /* Release：零开销 */
#endif
```

**改动频率**：加新外设时要来这里开对应模块。

### `STM32F103C8.ld` —— 链接脚本

告诉链接器每个段的 VMA（运行地址）和 LMA（存储地址）。

相比裸机版**多了三个段**：
```ld
.preinit_array / .init_array / .fini_array
```
因为官方启动文件会调 `__libc_init_array` 遍历这些段。缺了就 HardFault——踩坑详情见 `DEBUG_NOTES.md` 问题 3。

`.text` 里还补了 `*(.glue_7)` `*(.glue_7t)` `KEEP(*(.init))` `KEEP(*(.fini))`。

**改动频率**：几乎不动。除非做 bootloader 分区、换芯片型号、或要把函数放 RAM 里跑。

### `Makefile` —— 构建脚本

三个关键点：

**① 只编译用到的 HAL 模块**（不是整个库）
```makefile
C_SRC += $(HAL_DIR)/Src/stm32f1xx_hal_gpio.c
C_SRC += $(HAL_DIR)/Src/stm32f1xx_hal_uart.c
...
```
加新外设时要在这里加对应的 `.c`，同时在 `hal_conf.h` 里开宏。**两处都要改**。

**② 编译期宏定义**
```makefile
DEFS  = -DSTM32F103xB    # 告诉 CMSIS 头文件选哪个芯片的寄存器定义
DEFS += -DUSE_HAL_DRIVER # 启用 HAL（stm32f1xx.h 靠它决定是否 include hal.h）
```
`STM32F103xB` 对应 `CMSIS/Device/ST/STM32F1xx/Include/stm32f103xb.h`（中容量：64K/128K Flash）。型号写错 → 寄存器地址或外设数量不对。

**③ VPATH 解决多目录源文件**
```makefile
OBJS  = $(addprefix $(BUILD)/, $(notdir $(C_SRC:.c=.o)))
VPATH = $(sort $(dir $(C_SRC) $(AS_SRC)))
```
源文件散落在多个目录，`.o` 全部拍平放进 `build/`。`VPATH` 告诉 make 去哪些目录找源文件。

**注意**：拍平后**不同目录下同名文件会冲突**，目前没有这个问题。

### `compile_commands.json` —— 编辑器用，编译不用

`bear -- make clean all` 生成，记录每个 `.c` 的完整编译命令（include 路径 + 宏定义）。VS Code 的 clangd 靠它做跳转和补全。

**不影响编译**，删了也能 `make`。生成方法见 `BUILD.md`。

---

## 加一个新外设的完整步骤

以加 SPI 为例，**三处都要改**：

1. `Makefile` 加源文件
   ```makefile
   C_SRC += $(HAL_DIR)/Src/stm32f1xx_hal_spi.c
   ```
2. `stm32f1xx_hal_conf.h` 开模块
   ```c
   #define HAL_SPI_MODULE_ENABLED
   #ifdef HAL_SPI_MODULE_ENABLED
     #include "stm32f1xx_hal_spi.h"
   #endif
   ```
3. `main.c` 里初始化（别忘了先使能时钟 `__HAL_RCC_SPI1_CLK_ENABLE()` 和配置对应 GPIO 复用）

漏第 1 步 → 链接期 undefined reference。
漏第 2 步 → 编译期找不到类型定义。
漏时钟使能 → 编译烧录都正常，但外设完全没反应（寄存器写进去也不生效）。

---

## 当前工程体积

```
   text    data     bss     dec     hex
   4680      20     104    4804    12c4
```

- `text` 4680 B → Flash 占用（代码 + 只读数据），64K Flash 用了 7%
- `data` 20 B → 有初值的全局变量，**同时占 Flash 和 RAM**
- `bss` 104 B → 无初值的全局变量，只占 RAM

> 以上是 jammy + GCC 10.3 的实测值。换编译器版本时数字会小幅浮动，属正常。

对比裸机项目的 228 B——HAL 库的开销就在这。但 64K Flash 完全够用，实际项目不必为这点体积回退到裸机。

> 用了 `-ffunction-sections -fdata-sections` + `-Wl,--gc-sections`，未调用的 HAL 函数会被链接器丢弃，
> 所以体积远小于"整个 HAL 库"的大小。
