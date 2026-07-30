# HAL 工程踩坑记录与调试思路

搭建 STM32HAL 工程过程中遇到的三个问题，以及定位它们用到的方法。

---

## 问题 1：`assert_param` 未定义（链接报错）

**现象**：编译能过，链接时一堆 `undefined reference to 'assert_param'`。

**原因**：HAL 库源码里到处调 `assert_param()` 做参数检查，但这个宏是**留给用户在 `stm32f1xx_hal_conf.h` 里定义的**。官方 CubeMX 生成的 conf 文件里有，我们手写的 conf 里漏了。

**修复**：在 `src/stm32f1xx_hal_conf.h` 加上：
```c
#ifdef USE_FULL_ASSERT
  #define assert_param(expr) ((expr) ? (void)0 : assert_failed((uint8_t *)__FILE__, __LINE__))
  void assert_failed(uint8_t *file, uint32_t line);
#else
  #define assert_param(expr) ((void)0)   /* Release 模式：不检查，零开销 */
#endif
```

**教训**：`stm32f1xx_hal_conf.h` 不只是"模块开关面板"，它还要提供 HAL 库依赖的宏定义。缺任何一个都是链接期报错。

---

## 问题 2：HSE 外部晶振起不来（LED 不闪，电源灯亮）

**现象**：烧录成功、验证通过，但 LED 完全不闪。

**定位思路**：
1. 电源灯亮 → 供电正常，排除硬件供电问题
2. 烧录 verify 通过 → Flash 内容正确，排除烧录问题
3. → 所以是**代码跑起来了但卡在某处**

**原因**：`SystemClock_Config()` 里配置的是 HSE（外部 8MHz 晶振）+ PLL。`HAL_RCC_OscConfig()` 会**死等 HSE ready 标志位**。廉价 Blue Pill 的晶振可能虚焊或根本没焊，标志位永远不置位 → 卡在初始化里，永远到不了 blink 循环。

**修复**：改用 HSI（片内 8MHz 振荡器，一定可用，不依赖外部器件）：
```c
osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
osc.HSIState = RCC_HSI_ON;
osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
osc.PLL.PLLState = RCC_PLL_NONE;

clk.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
```

**教训**：新板子第一次点灯**永远先用 HSI**。跑通之后再切 HSE + PLL 上 72MHz，这样出问题时能确定是时钟配置的锅而不是别的。

> HSI 精度不如 HSE（±1% vs ±20ppm），UART 高波特率下可能误码，但 115200 够用。

---

## 问题 3：HardFault（LED 还是不闪，换成 HSI 之后）

**现象**：改成 HSI 之后 LED 依然不闪。

**定位过程**（这一套是通用方法，重点记）：

```bash
# 终端 1
make debug-server

# 终端 2
telnet localhost 4444      # 或用 gdb
> halt                     # 暂停 CPU
> reg pc                   # 读 PC，看卡在哪
> reg xPSR                 # 看 IPSR 位段，判断当前在哪个异常里
```

1. `halt` 后读 **PC** → 落在一个和 `main` 无关的地址
2. 读 **xPSR** → IPSR 位段非 0，说明 CPU **在异常处理模式**里，不是 Thread mode
3. 对照 `build/firmware.map` 或 `arm-none-eabi-objdump -d` → 确认落在 `HardFault_Handler` 的死循环里
4. 反汇编 `Reset_Handler` 往前追，看崩溃前执行到哪一步

**原因**：官方启动文件的 `Reset_Handler` 在调 `main` 之前会调 `__libc_init_array`：

```
:64   bl SystemInit
:78   LoopCopyDataInit      拷 .data
:93   LoopFillZerobss       清 .bss
:98   bl __libc_init_array  ← 崩在这里
:100  bl main
```

`__libc_init_array` 要遍历 `.init_array` 段里的函数指针，靠链接脚本定义的 `__init_array_start` / `__init_array_end` 确定范围。我们的链接脚本是从裸机项目直接拷来的，**没有这几个段**，边界符号值不确定 → 按垃圾地址跳转 → HardFault。

**修复**：在 `STM32F103C8.ld` 加上三个段：
```ld
.preinit_array :
{
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN(__preinit_array_end = .);
} > FLASH

.init_array :
{
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    PROVIDE_HIDDEN(__init_array_end = .);
} > FLASH

.fini_array :
{
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array*))
    PROVIDE_HIDDEN(__fini_array_end = .);
} > FLASH
```
同时 `.text` 里补 `*(.glue_7)` `*(.glue_7t)` `KEEP(*(.init))` `KEEP(*(.fini))`。

**教训**：**启动文件和链接脚本是配套的，不能混搭。**

裸机项目手写的启动文件里没有 `bl __libc_init_array`，所以裸机版链接脚本缺这些段也没事。换成官方启动文件后，这一行就要求链接脚本必须提供对应的段。

> 注意关键不在"段是不是空的"。纯 C 项目 `.init_array` 里确实是 0 个函数指针，但**只要边界符号正确（start == end），循环一次都不进就返回**；边界符号是垃圾值，空表也会乱跳。

---

## 三个问题的报错层级对比

| 问题 | 什么时候暴露 | 排查入口 |
|------|-------------|---------|
| `assert_param` 未定义 | **链接期** | 看链接器 undefined reference |
| HSE 起不来 | **运行期，卡死在初始化** | OpenOCD halt 读 PC，落在 RCC 等待循环里 |
| 缺 `.init_array` | **运行期，HardFault** | OpenOCD halt 读 PC + xPSR，确认在异常模式 |

---

## 通用调试套路（记这个）

### 板子没反应时的排查顺序

```
1. 电源灯亮吗            → 不亮：供电 / USB 线问题
2. 烧录 verify 过了吗    → 没过：SWD 连接 / Flash 保护 / OpenOCD 配置
3. halt 后 PC 在哪       → 这一步能区分下面三种情况
```

### PC 落点的含义

| PC 落在哪 | 说明 |
|-----------|------|
| `main` 里的循环 | 代码在跑，问题在外设配置（GPIO 模式、时钟没使能） |
| 某个 `while(...)` 等待标志位 | 死等某个硬件条件，条件永远不成立（HSE / UART flag） |
| `HardFault_Handler` | 非法内存访问 / 非法跳转 / 未对齐访问 |
| `Default_Handler` | 某个中断被使能了但你没写对应的 handler |

### 判断当前在不在异常里

读 `xPSR`：
- **IPSR 位段（bit[8:0]）== 0** → Thread mode，正常执行
- **IPSR 位段 != 0** → 在某个异常/中断处理里，数值就是异常号

顺带：**bit 24 是 T 位**，必须为 1（Thumb 状态）。如果是 0，说明发生了非法的 ARM/Thumb 状态切换。

### 把地址翻译成符号

```bash
# 反汇编找地址对应的函数
arm-none-eabi-objdump -d build/firmware.elf | less

# 直接查符号表
arm-none-eabi-nm -n build/firmware.elf | less

# 看段布局和符号地址（链接期的最终结果）
less build/firmware.map
```

---

## 附：weak 符号与 Default_Handler

官方启动文件对每个中断都做了 weak 定义：

```asm
Default_Handler:
    b .                                            /* 死循环 */

    .weak      USART1_IRQHandler
    .thumb_set USART1_IRQHandler, Default_Handler
```

- **你没写** `USART1_IRQHandler` → 用 weak 定义 → 向量表指向 `Default_Handler`（死循环）
- **你写了** → 强符号覆盖 weak → 向量表指向你的函数

这就是为什么 `main.c` 里写一个 `SysTick_Handler` 就能被自动调用：

```c
void SysTick_Handler(void)
{
    HAL_IncTick();      /* HAL_Delay() 依赖它计时，不写 HAL_Delay 会永久阻塞 */
}
```

不是魔法，是 weak 符号 + 向量表的配合。

**推论**：使能了某个中断但忘了写 handler，程序会卡在 `Default_Handler` 死循环里 —— 现象和"程序跑飞"一样，但 PC 落点完全不同，用上面的方法能一眼区分。
