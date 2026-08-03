# HAL 工程踩坑记录与调试思路

搭建 STM32HAL 工程过程中遇到的问题，以及定位它们用到的方法。

**记录了三个，但其中只有两个是真的** —— 问题 2 是一次误诊，后来被硬件实测推翻。
误诊那节没有删掉，因为「怎么误诊的」比「正确答案是什么」更值得记。

---

# ⚠️ 核心直觉 —— 全文最该记住的一条

> ## 链接期的错误，会以**运行期崩溃**的形式出现。
> ## 中间隔着两个表现**完全正常**的环节。

```
① 链接期    错误在这里产生      ← 链接器 exit=0，零警告
② 烧录期    忠实搬运错的东西    ← ** Verified OK **
③ 运行期    症状在这里暴露      ← HardFault
```

下面的**问题 3 就是活例子**：当时的排查直觉是「LED 不闪 → 是不是没烧进去？是不是时钟不对？」
—— 全在往 ② 和 ③ 找，而错误在 ①。绕了很久才定位到链接脚本。

### 👉 要养成的直觉

**遇到「烧录成功但行为诡异」，第一反应必须包括：产物本身可能就是错的。**

不要因为 `Verified OK` 就把 ELF 排除在嫌疑之外 —— 那三个「成功」信号各有各的边界：

| 看到的成功信号 | 它证明了什么 | 它**不**证明什么 |
|---|---|---|
| 链接器 `exit=0`，零警告 | ELF 结构合法、符号都能解析 | 段布局在语义上是对的 |
| `** Verified OK **` | Flash 里的字节 == ELF 里的字节（读回逐字节比对） | **ELF 本身是对的** |
| `** Resetting Target **` | 复位命令发出去了 | 复位后代码能跑通 |

**`Verified OK` 最容易误导 —— 它只保证「搬运无损」，不保证「货物正确」。**

> 这条直觉比本文任何一个具体踩坑都值钱：具体的坑（`.init` 段怎么拼、HSE 为什么不起振）
> 未来未必再遇到，但「错误在三步之前」这个排查方向会反复用到。

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

## 问题 2：❌ 误诊记录 —— 「HSE 晶振起不来」（结论是错的）

> ⚠️ **这一节保留下来当反面教材。** 当年的结论后来被硬件实测推翻 —— **晶振一直是好的**，
> 「LED 不闪」完全是问题 3 的 HardFault 造成的。
> 保留它是因为「凭推理下结论」这个错误本身，比正确答案更值得记。

**现象**：烧录成功、验证通过，但 LED 完全不闪。

**当年的定位思路**（标注每一步的可靠性）：

| 步骤 | 可靠性 |
|---|---|
| 1. 电源灯亮 → 供电正常 | ✅ 有依据 |
| 2. 烧录 verify 通过 → Flash 内容正确 | ⚠️ **只证明搬运无损，不证明 ELF 是对的**（见开头「核心直觉」） |
| 3. → 所以是「代码跑起来了但卡在某处」 | ❌ **纯推理，没有实测** |
| 4. → 时钟用了 HSE，大概是晶振虚焊 | ❌ **纯猜测** |

**当年的结论（错的）**：`HAL_RCC_OscConfig()` 会死等 HSE ready 标志位，廉价 Blue Pill 晶振虚焊 → 卡在初始化里，永远到不了 blink 循环。

### 这个结论错在哪 —— 两处，都有实证

**① HAL 根本不会死等。** `stm32f1xx_hal_rcc.c` 里 HSE 等待循环是带超时的：

```c
tickstart = HAL_GetTick();
while (__HAL_RCC_GET_FLAG(RCC_FLAG_HSERDY) == RESET) {
    if ((HAL_GetTick() - tickstart) > HSE_TIMEOUT_VALUE)
        return HAL_TIMEOUT;          /* HSE_STARTUP_TIMEOUT = 100U，即 100ms */
}
```

就算晶振真没焊，**100ms 后函数就返回 `HAL_TIMEOUT`**。当年的代码忽略了返回值，于是会继续用复位后的默认时钟（HSI 8MHz）往下跑 —— **LED 照样会闪**，只是快慢不对。所以「完全不闪」这个症状**不可能**由 HSE 失败造成。

**② 晶振本来就是好的。** 改回 HSE + PLL ×9 后串口实测：

```
clock : HSE 8MHz x9 -> 72MHz
SYSCLK: 72000000 Hz
PCLK1 : 36000000 Hz
PCLK2 : 72000000 Hz
```

### 真正的原因

**问题 3 的 HardFault。** 它发生在 `Reset_Handler` 里、`main` 之前 —— `SystemClock_Config()` 那段代码**压根没执行到**，时钟配成什么样都无所谓。

换成 HSI 那次「依然不闪」，不是因为「还有第二个问题」，而是因为**第一个诊断从头就没打中**。

### 真正该记的教训

| 错在哪 | 该怎么做 |
|---|---|
| 第 3 步用推理代替实测 | `halt` 读 PC —— 一条命令就能区分「卡在 RCC 等待循环」和「在 `HardFault_Handler`」 |
| 把「verify 通过」当成「产物没问题」 | 见开头「核心直觉」 |
| 忽略 HAL 函数返回值 | `HAL_RCC_OscConfig` / `HAL_RCC_ClockConfig` 都返回 `HAL_StatusTypeDef`，判它 |
| 改了 A、症状没消失，就以为「还有个 B」 | **先确认 A 真的是原因**，否则会串起一堆假因果 |

> 「新板子第一次点灯先用 HSI」这条建议本身没错（少一个变量），但当年它是从一个**错误诊断**里得出的 —— 结论对不代表推理过程对。

**当前代码**：已改回 HSE 8MHz × PLL9 = 72MHz，并保留 HSI 自动回退（因为 `HAL_RCC_OscConfig` 有超时会返回错误，判返回值再回退是安全的）。见 `src/main.c` 的 `SystemClock_Config()`。

---

## 问题 3：✅ HardFault —— 「LED 不闪」的**唯一真实原因**

**现象**：改成 HSI 之后 LED 依然不闪。

> 事后看清了：问题 2 换 HSI 是无效操作，症状从头到尾只有这一个原因。
> 这一节的定位过程之所以有效，就是因为**它是实测出来的**，不是推理出来的。

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

`__libc_init_array` 干两件事：遍历 `.init_array` / `.preinit_array` 里的函数指针，以及**无条件调用 `_init`**。

我们的链接脚本是从裸机项目直接拷来的，缺了官方启动文件需要的段。**崩的是第二件事** —— 用残缺脚本重新链接后 `objdump` 对比，实测结论：

| | 遍历 array | `bl _init` |
|---|---|---|
| 缺段时的符号值 | 四个边界符号全解析成 **0**（newlib 里声明为 weak，未定义即 0） | — |
| 实际行为 | `0 - 0 = 0` → `beq` 跳过循环，**一次都没进** | **无条件执行，躲不掉** |

真正的原因是 `_init` 残废了。它的函数体是**拼出来的** —— 开头 `push` 来自 `crti.o`，结尾 `pop` + `bx lr` 来自 `crtn.o`。链接脚本里没有 `KEEP(*(.init))`，`--gc-sections` 就把 `crtn.o` 那半截当垃圾回收了（纯收尾指令，没有任何符号引用它）：

```
残缺版 _init：                     正常版 _init：
  push {r3,r4,r5,r6,r7,lr}           push {r3,r4,r5,r6,r7,lr}
  nop                                nop
  ← 到这就没了，没有返回指令          pop  {r3,r4,r5,r6,r7}
                                     pop  {r3}
                                     mov  lr, r3
                                     bx   lr
```

`_init` 执行完 `nop` **顺势往下掉** → 落进同样残废的 `_fini`（也只剩 push+nop）→ 掉出段尾 → HardFault。

**修复**：分两部分，**第一部分才是治崩的**。

**① `.text` 输出段里补 `KEEP`** —— 保住 `_init` / `_fini` 的完整函数体，这是主因：

```ld
*(.glue_7)
*(.glue_7t)
KEEP(*(.init))
KEEP(*(.fini))
```

**② 加上三个 array 段** —— 这部分**不治崩**（缺了只是静默失效），但缺了 `.init_array` 里的条目永远不会被调用：

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

**教训**：**启动文件和链接脚本是配套的，不能混搭。**

裸机项目手写的启动文件里没有 `bl __libc_init_array`，所以裸机版链接脚本缺这些段也没事。换成官方启动文件后，这一行就要求链接脚本必须提供对应的段。

**更能迁移的一条**：`--gc-sections` 会删掉「没有任何符号引用、但必须存在」的东西，`KEEP()` 就是用来保护它们的。本项目的 `.isr_vector` 是同一个道理 —— 全项目没有一行 C 代码引用向量表，只有硬件上电时去读，没有 `KEEP` 就会被删掉、芯片压根起不来。详见 `STMTest/LEARNING.md` 第 3 层的「`KEEP` 与 `--gc-sections`」一节。

> **两个容易记错的点**（都是实测纠正过的）：
>
> 1. **`.init_array` 在纯 C 项目里不是空的。** 本工程实测有 4 字节 = 1 个条目，内容
>    `0x08000175` = `frame_dummy` + Thumb 位，由 `crtbegin.o` 提供（栈回溯帧信息注册）。
>    `.fini_array` 同样有 1 个条目（`__do_global_dtors_aux`）。
> 2. **边界符号是垃圾值不会导致乱跳。** newlib 把这四个符号声明为 weak，未定义就解析成 0，
>    `0 - 0 = 0` 循环直接跳过 —— 那是**静默失效**，不是崩溃。崩的是无条件的 `bl _init`。

---

## 三个问题的报错层级对比

| 问题 | 什么时候暴露 | 排查入口 |
|------|-------------|---------|
| `assert_param` 未定义 | **链接期** | 看链接器 undefined reference |
| ❌ 「HSE 起不来」 | **不存在** —— 误诊，见问题 2 | 本该 `halt` 读 PC 验证，当年跳过了这步 |
| 缺 `KEEP(*(.init))` | **运行期，HardFault**（错误其实在链接期就产生了） | OpenOCD halt 读 PC + xPSR，确认在异常模式 |

**所以真实的问题只有两个**：一个链接期报错（`assert_param`），一个链接期埋雷 / 运行期爆炸（缺 `KEEP`）。中间那个是自己臆造出来的。

---

## 通用调试套路（记这个）

### 板子没反应时的排查顺序

```
1. 电源灯亮吗            → 不亮：供电 / USB 线问题
2. 烧录 verify 过了吗    → 没过：SWD 连接 / Flash 保护 / OpenOCD 配置
3. halt 后 PC 在哪       → 这一步能区分下面三种情况
```

> 排查顺序的第 2 步有个陷阱：**verify 过了不代表产物是对的**。见开头的「核心直觉」。

### PC 落点的含义

| PC 落在哪 | 说明 |
|-----------|------|
| `main` 里的循环 | 代码在跑，问题在外设配置（GPIO 模式、时钟没使能） |
| 某个 `while(...)` 等待标志位 | 死等某个硬件条件，条件永远不成立 |
| `HardFault_Handler` | 非法内存访问 / 非法跳转 / 未对齐访问 |
| `Default_Handler` | 某个中断被使能了但你没写对应的 handler |

> **「死等」要看是谁写的循环**：裸机手写的 `while (!(FLAG))` 才会真的卡死；
> **HAL 库的等待循环基本都带超时**（`HSE_TIMEOUT_VALUE`、`HAL_MAX_DELAY` 之外的具体值），
> 到时间返回 `HAL_TIMEOUT` 而不是挂死。所以看到「卡住」先想清楚卡在谁的代码里 ——
> 问题 2 的误诊就是把 HAL 当成会死等了。

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
