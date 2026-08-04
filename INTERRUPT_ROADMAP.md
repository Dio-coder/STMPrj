# 中断机制学习路线

从「知道向量表是一张表」到「理解中断、能调试竞态」的七步路径。全部在 `STMTest` 裸机工程里做，不用 HAL。

> **前置**：已掌握链接期段布局（`STMTest/LEARNING.md` 第 3 层）和运行期内存模型（`RUNTIME_MODEL.md`）。
> 本文所有寄存器地址、IRQ 编号、结构体偏移都从 `STM32CubeF1` 的 CMSIS 头文件**核实过**，不是凭记忆写的。

---

## 零、为什么从中断开始

不用 RTOS 的裸机程序，结构只有两样东西：**主循环 + 中断服务程序**。所以中断就是并发模型的全部。

更实际的理由是：**它把已经学过的静态知识全部激活。**

| 已掌握的（静态） | 中断让它变成活的机制 |
|---|---|
| 向量表是一张地址表（数据，不是代码） | 硬件在**运行期**从表里取地址跳转 —— 本质是 `blx` 的硬件版 |
| `.text` 从 `0x08000040` 开始，因为表占 `0x40` | 加了外设中断条目，表变长，`.text` **会后移**（步骤 1 亲眼验证） |
| SP、`push {lr}`、`sub sp` | 异常入口**硬件自动压 8 个字 = 32 字节**，不经过任何指令 |
| xPSR / IPSR（调 HardFault 时读过） | IPSR = 「当前在第几号异常里」的编号 |
| weak 符号 + `Default_Handler` | 第一次真正**覆盖**一个 weak handler |
| `volatile` 为什么必须有 | 现在的代码其实用不着它 —— **只有中断存在时才真正需要** |

---

## 一、先备齐三份手册

**这一步别跳过。** 调试器能告诉你「寄存器里是这个值」，不会告诉你「这个值是错的」。

| 文档 | 编号 | 管什么 | 什么时候查 |
|---|---|---|---|
| **Reference Manual** | **RM0008** | 外设寄存器：RCC / GPIO / AFIO / **EXTI** / TIM / USART | 配外设时 |
| **Programming Manual** | **PM0056** | **Cortex-M3 内核**：**NVIC**、SCB、异常模型、故障状态寄存器、指令集 | 配中断优先级、分析 HardFault 时 |
| Datasheet | DS5319 | 引脚定义、电气参数、封装 | 接线时 |

> ⚠️ **最容易浪费时间的一点**：**NVIC 和 SCB 不在 RM0008 里，在 PM0056。**
> 在 RM0008 里翻 NVIC 会翻半天找不到 —— 因为 NVIC 是 ARM 内核的一部分，不是 ST 的外设。
> 同理 `CFSR`/`HFSR`/`BFAR`（步骤 6 要用）也全在 PM0056。

另外记得看 **errata（勘误表）**，ST 的芯片有一批已知硬件 bug。

---

## 二、调试环境速查（已验证可用）

需要**两个终端**，OpenOCD 要常驻：

```bash
# 终端 1 —— 停在 "Listening on port 3333"，别关
cd ~/STMPrj/STMTest && make debug-server

# 终端 2
cd ~/STMPrj/STMTest && make debug
```

> OpenOCD **独占 ST-Link**。它在跑的时候 `make flash` 会报 `open failed`。

### 本项目实测可用的调试手法

```gdb
monitor reset halt                     # 复位并暂停
maintenance flush register-cache       # ★ 复位后必须刷缓存，否则 $pc 还是旧值

break *SysTick_Handler                 # ★ 加 * 停在函数序言之前，才看得到未污染的异常帧
x/8xw $sp                              # 异常帧：r0 r1 r2 r3 r12 LR PC xPSR
info symbol *(unsigned int*)($sp+24)   # 反查第 7 个字（被打断的 PC）属于哪个函数

x/1xw 0xE000E100                       # NVIC_ISER0 —— 看中断到底使能了没
x/6xw 0x40010400                       # EXTI 全部 6 个寄存器
p/x $sp                                # 栈深度 = 0x20005000 - $sp
```

### 三个已踩过的坑

| 现象 | 原因 / 解法 |
|---|---|
| `monitor reset halt` 后 `$pc` 是旧值 | gdb 有寄存器缓存 → `maintenance flush register-cache` |
| `info registers xpsr` 报 `Invalid register` | 这个 target 上 gdb 不这么暴露它。从异常帧第 8 个字读，或看 OpenOCD 自己打印的 `xPSR:` |
| `break X_IRQHandler` 停在序言之后，栈帧被污染 | 用 `break *X_IRQHandler`（加 `*`） |

硬件资源：**6 个硬件断点、4 个硬件观察点**（`watch` 用得上，步骤 5 要靠它）。

---

## 三、七步路线图

### 步骤 0（可选但推荐）：给 `STMTest` 加寄存器级 UART TX

**为什么做**：`STMTest` 现在只有 LED，调中断只能靠闪灯 + GDB 单步，效率低。有了串口能直接打印状态。

**为什么这不算「学外设用法」**：手写 `USART_BRR` 的**整数/小数分频**是真底层 —— 你已经在 HAL 工程见过 72MHz 下 `USARTDIV = 39.0625` 正好能整除的结论，这次自己算一遍。约 20 行代码。

**要动**：`include/stm32f103_regs.h` 加 `USART_TypeDef`、`src/main.c` 加 `uart_init()` / `uart_putc()`

**验证**：`minicom -b 115200 -D /dev/ttyUSB0` 看到输出

---

### 步骤 1：手工扩展向量表 ★ 最能承接已有知识

**目标**：让 `STMTest` 的向量表容纳 `EXTI0_IRQHandler`。

**当前状态**（实测）：

```
STMTest/src/startup_stm32f103.s   16 个 .word  →  .isr_vector = 0x40 字节
                                                  .text 从 0x08000040 开始
```

**要做**：`EXTI0_IRQn = 6` → 向量表索引 = `16 + 6 = 22` → 需要表有 **23 个条目**。
所以在 `SysTick_Handler` 那行后面补 7 个条目（索引 16~22），前 6 个填 `0` 或 `Default_Handler`，第 7 个填 `EXTI0_IRQHandler`。

顺便给它加 weak 定义（照抄官方启动文件的写法）：

```asm
    .weak      EXTI0_IRQHandler
    .thumb_set EXTI0_IRQHandler, Default_Handler
```

**验证 —— 这是本步骤的重点**：

```bash
arm-none-eabi-objdump -h build/firmware.elf | grep -E 'isr_vector|\.text'
```

**预期看到**：`.isr_vector` 从 `0x40` → **`0x5C`**（23×4=92=0x5C），`.text` 起始地址从 `0x08000040` → **`0x0800005C`**。

> 这正好验证 `LEARNING.md` 第 203 行你自己写下的预言：
> 「如果以后向量表加外设中断条目变长，`.text` 起始地址会自动后移 —— 链接器会算，你不用手动对。」

再看表的内容：`x/23xw 0x08000000`，第 23 个字（偏移 `0x58`）应该指向你的 handler。

---

### 步骤 2：点亮第一个中断（EXTI）

**关键**：先用**软件触发**，不用接任何硬件。`EXTI->SWIER` 写 1 就能造出一次中断 —— 把「配置链路对不对」和「按键接线对不对」两个问题分开。

**四件事缺一不可**（这是最容易漏的地方）：

| # | 干什么 | 寄存器 |
|---|---|---|
| 1 | 开 AFIO 时钟 | `RCC->APB2ENR` 的 `AFIOEN` 位 |
| 2 | 选哪个端口的 Pin0 接到 EXTI0 | `AFIO->EXTICR[0]` 低 4 位（0=PA0, 2=PC0…） |
| 3 | 解除 EXTI 屏蔽 + 选触发边沿 | `EXTI->IMR` 置位、`EXTI->RTSR` / `FTSR` |
| 4 | **在 NVIC 里使能这个 IRQ** | `NVIC_ISER0`（`0xE000E100`）的 **bit 6** |

**ISR 里必须清 pending 标志**，否则中断会无限重入：

```c
void EXTI0_IRQHandler(void) {
    EXTI->PR = (1u << 0);    /* 写 1 清除，不是写 0 */
    /* ... */
}
```

**验证**：
- 主循环里写 `EXTI->SWIER = 1;` → 应该进 handler（LED 翻转或串口打印）
- `x/1xw 0xE000E100` 确认 bit 6 = 1
- `x/6xw 0x40010400` 看 EXTI 六个寄存器的实际值
- **逐个注释掉上面四件事**，看哪个缺了就不触发 —— 这比读文档记得牢

**做完再接真实按键**（PA0 接按键到 GND，配内部上拉，下降沿触发）。

---

### 步骤 3：异常栈帧 ★ 承接 RUNTIME_MODEL.md

**目标**：亲眼看到硬件自动压的 8 个字。

**做法**：

```gdb
break *EXTI0_IRQHandler
continue
x/8xw $sp
```

**预期**（本项目在 `SysTick_Handler` 上已实测过，格式一样）：

```
$sp+0x00:  r0
$sp+0x04:  r1
$sp+0x08:  r2
$sp+0x0C:  r3
$sp+0x10:  r12
$sp+0x14:  LR      ← 返回地址（带 Thumb 位）
$sp+0x18:  PC      ← 被打断的那条指令
$sp+0x1C:  xPSR    ← bit24 = T 位；bit[8:0] = IPSR
```

**要观察到的三件事**：

1. `info symbol *(unsigned int*)($sp+24)` → 反查出被打断的函数。触发时机不同，落点不同
2. **进 handler 前后 SP 差 32 字节** —— 这 32 字节没有任何指令压过，纯硬件行为
3. 栈上的 xPSR，IPSR 位段 = **0**（被打断的代码在 Thread mode）；而 handler 内部当前的 xPSR，IPSR = **异常号**

**对照 `RUNTIME_MODEL.md` 第五节**：那里讲的「硬件动 SP 只有复位和进出异常两处」，这一步就是第二处的实证。

---

### 步骤 4：优先级与抢占

**F103 的坑**：Cortex-M3 优先级寄存器是 8 bit，但**F103 只实现高 4 bit**（16 个优先级）。低 4 位写了无效。

**要理解**：
- **抢占优先级**（preemption）vs **子优先级**（sub priority）—— 只有抢占优先级不同才能嵌套
- 优先级分组 `SCB->AIRCR` 决定 4 个 bit 怎么切分
- **数值越小优先级越高**（和直觉相反）

**验证**：配两个中断（EXTI0 + SysTick），给不同抢占优先级，在两个 handler 里各下断点，看谁能打断谁。嵌套时 `$sp` 会再降 32 字节 —— **两层异常帧**。

---

### 步骤 5：原子性与临界区 ★ 分水岭

**这一步开始才是「理解中断」而不是「会用中断」。**

**场景**：中断和主循环共享一个计数器。

**要搞清的四件事**：

| 概念 | 关键点 |
|---|---|
| `volatile` 够不够 | **不够**。它只保证「每次都真的去内存读写」，**不保证操作是原子的** |
| 读-改-写不是原子的 | `cnt++` 编译成 `ldr` / `add` / `str` 三条指令，中断可以插在中间 |
| 临界区 | `__disable_irq()` / `__enable_irq()`，要**尽可能短** |
| 内存屏障 | `__DMB()` / `__DSB()` —— 某些外设寄存器写完需要屏障才保证生效 |

**验证手法**：
- `objdump -d` 看 `cnt++` 到底编译成几条指令 —— **眼见为实**
- 用**硬件观察点** `watch cnt`（有 4 个可用），看谁在改它
- 制造竞态：中断里和主循环里同时 `cnt++`，跑一会儿看总数对不对

---

### 步骤 6：HardFault 深度分析

**你已经踩过一次 HardFault**（`DEBUG_NOTES.md` 问题 3），当时靠反汇编 `Reset_Handler` 往前追定位。这一步学**通用手法**。

**四个故障状态寄存器**（全在 **PM0056**，不在 RM0008）：

| 寄存器 | 地址 | 说明 |
|---|---|---|
| `CFSR` | `0xE000ED28` | 可配置故障状态：分 MemManage / BusFault / UsageFault 三段 |
| `HFSR` | `0xE000ED2C` | HardFault 状态，`FORCED` 位表示是从其他故障升级来的 |
| `MMFAR` | `0xE000ED34` | MemManage 出错的地址 |
| `BFAR` | `0xE000ED38` | BusFault 出错的地址 |

**核心手法 —— 从异常帧反推出错指令**：

```gdb
break *HardFault_Handler
continue
x/8xw $sp                              # 异常帧
info symbol *(unsigned int*)($sp+24)   # ★ 第 7 个字 = 出错的那条指令
x/1xw 0xE000ED28                       # CFSR：什么类型的故障
x/1xw 0xE000ED38                       # BFAR：访问哪个非法地址
```

比当年「反汇编往前猜」快得多，而且是通用的。

**练习**：故意造几种不同的故障，看 CFSR 的位怎么变 —— 读非法地址、跳到非 Thumb 地址、未对齐访问。

---

## 四、寄存器地址速查（已从 CMSIS 头文件核实）

### 外设（ST 的，查 RM0008）

| 外设 | 基地址 | 寄存器（按偏移顺序） |
|---|---|---|
| `AFIO` | `0x40010000` | `EVCR` `MAPR` `EXTICR[4]`(0x08~0x14) `MAPR2` |
| `EXTI` | `0x40010400` | `IMR` `EMR` `RTSR` `FTSR` **`SWIER`**(0x10) `PR`(0x14) |
| `GPIOA` | `0x40010800` | — |
| `GPIOC` | `0x40011000` | — |
| `RCC` | `0x40021000` | — |

### 内核（ARM 的，查 PM0056）

| 模块 | 地址 | 用途 |
|---|---|---|
| `SysTick` | `0xE000E010` | 系统节拍 |
| **`NVIC_ISER0`** | **`0xE000E100`** | 中断使能（IRQ 0~31，每位一个） |
| `NVIC_ISER1` | `0xE000E104` | IRQ 32~63 |
| `NVIC_IPR` | `0xE000E400` 起 | 优先级，每 IRQ 一字节（F103 只用高 4 bit） |
| `SCB->ICSR` | `0xE000ED04` | 当前异常号、pending 状态 |
| `SCB->VTOR` | `0xE000ED08` | 向量表基址（**可以把表搬到 RAM**） |
| `SCB->AIRCR` | `0xE000ED0C` | 优先级分组 |
| `SCB->CFSR` | `0xE000ED28` | 故障状态 |
| `SCB->HFSR` | `0xE000ED2C` | HardFault 状态 |
| `SCB->BFAR` | `0xE000ED38` | BusFault 地址 |

### IRQ 编号 → 向量表索引

**向量表索引 = 16 + IRQn**（IRQn ≥ 0 的外设中断）

| 中断 | IRQn | 向量表索引 | 表内偏移 |
|---|---|---|---|
| **EXTI0** | **6** | **22** | **0x58** |
| EXTI1 | 7 | 23 | 0x5C |
| EXTI2 | 8 | 24 | 0x60 |
| EXTI9_5 | 23 | 39 | 0x9C |
| TIM2 | 28 | 44 | 0xB0 |
| EXTI15_10 | 40 | 56 | 0xE0 |

内核异常用负数（不经过 NVIC 使能）：`HardFault = -13`（异常号 3）、`PendSV = -2`（14）、`SysTick = -1`（15）。

---

## 五、坑预警

| 坑 | 症状 | 原因 |
|---|---|---|
| 忘了 `NVIC_ISER` 使能 | EXTI 的 `PR` 位置起来了，但 handler 从不进 | EXTI 只负责产生请求，NVIC 才决定送不送给 CPU |
| 忘了开 **AFIO 时钟** | `AFIO->EXTICR` 写进去读回来是 0 | 外设时钟没开，寄存器压根不响应 |
| ISR 里忘了清 `EXTI->PR` | 中断无限重入，程序像卡死 | pending 位不清，NVIC 立刻再次触发 |
| 清 `PR` 用了 `&= ~` | 不生效 | `PR` 是**写 1 清除**，要 `EXTI->PR = (1<<n)` |
| handler 名字拼错 | 不报错，但中断进 `Default_Handler` 死循环 | weak 符号没被覆盖 —— `halt` 读 PC 能一眼看出 |
| 优先级数值搞反 | 高优先级中断被低的挡住 | **数值越小优先级越高** |
| 优先级低 4 位写了没反应 | 两个不同优先级表现一样 | F103 只实现高 4 bit |
| 共享变量只加 `volatile` | 偶发的数据错乱 | `volatile` ≠ 原子。见步骤 5 |
| ISR 里调 `HAL_Delay` / 长循环 | 系统卡顿、其他中断丢失 | ISR 要短 —— 置标志位交给主循环 |
| 中断嵌套深了栈溢出 | 全局变量莫名被改 | 每层异常吃 32 字节，加上 handler 自己的栈帧。见 `RUNTIME_MODEL.md` 第二节 |

---

## 六、学完之后通向哪里

**你学的东西直通 RTOS 内部** —— RTOS 不是另一套体系，它的调度器就是靠中断实现的：

| RTOS 机制 | 底层就是 |
|---|---|
| 时间片轮转 | **SysTick 中断** |
| 任务切换 | **PendSV 中断** handler 里手动改 SP |
| 保存/恢复任务上下文 | **就是那 8 个字的异常帧** + 手动压另外 8 个寄存器 |
| 每个任务独立栈 | **MSP / PSP 双栈指针**切换 |
| 临界区 | `__disable_irq()` 或 `BASEPRI` 寄存器 |

所以步骤 3 里 `x/8xw $sp` 看到的那 32 字节，**就是 RTOS 上下文切换搬来搬去的东西**。裸机中断彻底搞懂之后，FreeRTOS 的 `port.c` 是能读懂的 —— 反过来先学 RTOS 的人往往读不懂。

### 中断之后值得深入的方向

| 方向 | 为什么算底层 |
|---|---|
| **DMA** | 总线仲裁、和 CPU 争 AHB、DMA 改了内存 CPU 未必立刻看到 |
| **TIM 深入** | 不是「产生 PWM」，而是预分频/自动重载/**影子寄存器**的更新时序 |
| **低功耗** | sleep/stop/standby、唤醒源、时钟门控 |
| **`SCB->VTOR` 搬表到 RAM** | Bootloader / IAP 的基础，直接用到链接脚本知识 |

---

## 附：明天的第一件事

```bash
cd ~/STMPrj/STMTest

# 1. 记下当前基准，改完好对比
arm-none-eabi-objdump -h build/firmware.elf | grep -E 'isr_vector|\.text'
#    预期现在是：.isr_vector 0x40 @ 0x08000000    .text @ 0x08000040

# 2. 打开启动文件，看那 16 个 .word
grep -n '\.word' src/startup_stm32f103.s
```

然后按**步骤 1** 扩表 —— 目标是让 `.isr_vector` 变成 `0x5C`、`.text` 挪到 `0x0800005C`。

改完 `make clean all` 再跑一次上面第 1 条命令对比。**这一步不需要接任何硬件。**
