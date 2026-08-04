# STM32 裸机开发学习地图

用开源工具链（GNU ARM + OpenOCD + Make）在 Linux/WSL2 下做 STM32F103C8T6 裸机开发。
本文是知识地图 + 项目导读，配合仓库里的代码一起学。

> 目标板：STM32F103C8T6（蓝色药丸），ARM Cortex-M3，Flash 64K / RAM 20K

---

## 项目文件导读

先知道每个文件是干什么的，学的时候对照着看：

| 文件 | 作用 | 对应知识层 |
|------|------|-----------|
| `STM32F103C8.ld` | 链接脚本：定义内存布局、段的排列 | 第 3 层 |
| `src/startup_stm32f103.s` | 启动文件：向量表 + Reset_Handler | 第 1 层 |
| `include/stm32f103_regs.h` | 寄存器定义（手写迷你版 CMSIS） | 第 1 层 |
| `src/main.c` | 主程序：PC13 点灯（寄存器级） | 第 1 / 2 层 |
| `Makefile` | 构建脚本：编译 / 烧录 / 调试 | 第 2 / 4 / 5 层 |
| `build/firmware.map` | 内存映射表（编译后生成） | 第 3 层 |

---

## 知识地图（按依赖顺序）

### 第 0 层：C 语言与计算机底层基础（前置）

地基，不牢后面全是玄学。

- **指针与内存** — `volatile`、`uint32_t *`、结构体内存布局。`GPIOC->ODR` 本质就是"往固定地址写数据"
- **位运算** — `|=`、`&= ~`、`<<`、`^=`。整个寄存器操作全靠它
- **数据表示** — 十六进制、二进制、字节序、对齐（alignment）
- **`volatile` 为什么必须有** — 嵌入式新手最大的坑：编译器优化会把寄存器读写"优化"掉

### 第 1 层：MCU 硬件与体系结构

- **ARM Cortex-M3 架构** — 寄存器组、Thumb 指令集、栈的增长方向（向下）
- **内存映射 I/O（MMIO）** — 为什么外设能当内存访问，`0x40021000` 这些地址从哪来
- **启动流程** — 上电 → 读 MSP → 读复位向量 → 跳转 → 初始化 → `main()`
- **中断与向量表** — vector table 结构，`.weak` 弱符号机制
- **查数据手册** — Reference Manual (RM0008) + Datasheet，寄存器地址与位定义的唯一来源

#### 深入笔记 · 启动全流程（重点）

**向量表 vs 启动代码：两个不同性质的东西**

| | 是什么 | 段 | 作用 |
|---|---|---|---|
| 向量表 | 一张**地址表**（数据） | `.isr_vector` | 存"谁在哪"的路标 |
| Reset_Handler | 一段**代码**（指令） | `.text` | 搬 .data、清 .bss、调 main |

向量表第 1 项**只是存了 Reset_Handler 的地址**（指针），本身不是代码。**表指向代码，表不是代码。**

**硬件是"傻"的——只认固定物理地址**

硬件不认识"向量表""启动文件""C 语言"，这些都是人和工具的概念。硬件上电只会一件事：
```
读物理地址 0x08000000 → 装入 SP   (读到 00 50 00 20 = 0x20005000 = _estack)
读物理地址 0x08000004 → 装入 PC   (读到 41 00 00 08 = 0x08000041 = Reset_Handler)
```
读到什么就当 SP/PC 用。上电这一刻**只读头两项**；向量表其余项（NMI/HardFault/SysTick…）是**运行中对应中断发生时**硬件才去表里取。

**那两个地址为什么"恰好"是对的？—— 链接排布 → 烧录写入 → 硬件读取 三步因果链**

```
① 链接时 (PC 上, ld 按 .ld 脚本)   —— 排座位
   .ld 说 FLASH 从 0x08000000 起, .isr_vector 放最前
   → 向量表地址被定为 0x08000000, 产出 firmware.bin(第0字节就是向量表)

② 烧录时 (OpenOCD + ST-Link, 走 SWD) —— 物理写入
   按 LMA(加载地址) 把 bin 从 0x08000000 起逐字节写进 Flash
   → bin 偏移 0 → 物理地址 0x08000000;  偏移 4 → 0x08000004
   (烧录看 LMA 不看 VMA; 所以 .data 初值烧到 Flash 而非 RAM)

③ 上电时 (硬件, 写死)              —— 闭眼睛读
   读 0x08000000/04 → 拿到上面写好的 SP 和 Reset_Handler 地址
```
> 关键洞察：**硬件是傻的，聪明的是链接脚本 + 烧录流程**。是这两步人为保证了"硬件闭眼读的地址上，正好有对的东西"。链接器只排布地址(不碰芯片)，OpenOCD 才真正写 Flash——两个独立步骤、两个工具。

**Thumb bit：向量表里的 +1（0x08000041）不是 PC 自增**

- 顺序执行时 PC 自增 = **+2 或 +4**（Thumb 指令宽度），**从不 +1**（+1 会落到指令中间）。
- 向量表存的值 `0x08000041` 那个 `+1` 是 **Thumb bit**（bit0=1 表示目标是 Thumb 指令）。
- CPU 装入 PC 时**剥掉 bit0** → 实际 `PC = 0x08000040`（偶数），bit0 记到 **xPSR 的 T 位**。
- 实测铁证：烧录复位后 `pc = 0x08000040`（偶数，非 41）、`xPSR = 0x01000000`（bit24=T位=1）。存进去带 1，进 PC 不带 1，那个 1 跑到 T 位去了。

**完整启动流程**
```
上电/复位
 ├─ 硬件读 0x08000000 → SP  (= _estack = 0x20005000)
 └─ 硬件读 0x08000004 → PC  (= 0x08000040, T位置1)
      ↓
 进入 Reset_Handler (.text 代码)
 ├─ 拷贝 .data (Flash→RAM)   ← 用 _sidata(源) / _sdata,_edata(目标范围)
 ├─ 清零 .bss                 ← 用 _sbss / _ebss
 └─ bl main
      ↓
 你的 main() 开始跑
```

#### 深入笔记 · 总线结构与外设地址

**STM32F103 三条总线（一张图够了）**
```
CPU (Cortex-M3)
 │
 ├── AHB 总线 (高速) ─── 0x40020000
 │    ├── RCC     0x40021000   ← 时钟控制
 │    ├── DMA     0x40020000
 │    └── ...
 │
 ├── APB2 总线 (高速外设) ─── 0x40010000
 │    ├── GPIOA   0x40010800   ← PA0~PA15
 │    ├── GPIOB   0x40010C00   ← PB0~PB15
 │    ├── GPIOC   0x40011000   ← PC0~PC15
 │    ├── USART1  0x40013800   ← 串口1
 │    ├── SPI1    0x40013000
 │    ├── TIM1    0x40012C00
 │    └── ...
 │
 └── APB1 总线 (低速外设) ─── 0x40000000
      ├── USART2  0x40004400
      ├── USART3  0x40004800
      ├── I2C1    0x40005400
      ├── TIM2    0x40000000
      └── ...
```

**三件实用的事**：

1. **外设挂在哪条总线 → 决定开哪个时钟**：
   APB2 → `RCC->APB2ENR`（GPIOA~G, USART1, SPI1, TIM1）；
   APB1 → `RCC->APB1ENR`（USART2/3, I2C, TIM2~7）；
   AHB → `RCC->AHBENR`（DMA, SDIO）

2. **基地址 + 偏移 = 寄存器地址**：
   `GPIOC_BASE(0x40011000) + 0x0C(ODR偏移) = 0x4001100C`

3. **总线速度不同 → 影响波特率计算**：
   AHB/APB2 = 72MHz，APB1 = 36MHz。USART1(APB2) 和 USART2(APB1) 配同样波特率，寄存器写的值不一样。

### 第 2 层：编译工具链（GNU Toolchain）

标题里"开源工具链"的核心。

| 工具 | 作用 |
|------|------|
| `arm-none-eabi-gcc` | 编译 C → 汇编 → 目标文件 |
| `arm-none-eabi-as` | 汇编器 |
| `arm-none-eabi-ld` | 链接器 |
| `arm-none-eabi-objcopy` | ELF → bin/hex |
| `arm-none-eabi-objdump` | 反汇编、看段（调试神器） |
| `arm-none-eabi-size` | 看 flash/ram 占用 |
| `arm-none-eabi-nm` | 看符号表 |

要理解的概念：
- **编译四阶段** — 预处理 → 编译 → 汇编 → 链接
- **交叉编译** — 为什么是 `arm-none-eabi` 而非系统 `gcc`。`none`=无 OS，`eabi`=嵌入式 ABI
- **编译 flag** — `-mcpu`、`-mthumb`、`-Og`、`--specs=nosys.specs`
- **newlib / newlib-nano** — 裸机环境下的精简 C 标准库

#### 深入笔记 · gcc 是驱动程序，不是链接器

`arm-none-eabi-gcc` 其实是一个**驱动程序（driver）**，它根据参数自动调后面真正干活的工具：

| 你敲的命令 | gcc 实际调了谁 | 干什么 |
|---|---|---|
| `gcc -c main.c` | 编译器(cc1) + 汇编器(as) | C → `.o` 目标文件 |
| `gcc -T xxx.ld main.o startup.o -o firmware.elf` | **链接器(ld)** | 多个 `.o` → ELF |

**`.ld` 脚本是写给链接器（ld）的**，不是给编译器的。`-T STM32F103C8.ld` 这个参数 gcc 自己不看，只是转手递给 `ld`。gcc 是传话的，ld 才是真正读 `.ld` 排地址的。

### 第 3 层：链接与内存布局（最易忽略但最关键）

- **链接脚本语法** — `MEMORY`、`SECTIONS`、`.text/.data/.bss/.rodata`
- **LMA vs VMA** — 加载地址 vs 运行地址。为什么 `.data` 存 Flash 却跑在 RAM
- **段的概念** — 代码 / 只读数据 / 初始化数据 / 未初始化数据分别放哪
- **Map 文件怎么读** — 每个符号占多少、在哪
- **`--gc-sections`** — 怎么把没用的函数从固件里删掉
- **`KEEP()`** — 反过来保护「没人引用但必须存在」的段。**向量表就靠它活着**

> 本层讲的是**链接期**：地址怎么排出来。
> 程序**跑起来之后**的部分 —— 栈/堆/静态区三个区域、`static` 到底改变了什么、
> PC/SP/LR 各管什么、函数怎么调用和返回 —— 见根目录的 **`RUNTIME_MODEL.md`**。

#### 深入笔记 · 绝对地址、ELF 与 BIN（重点）

**编译时地址未定，链接时才定（relocatable → executable）**

`.o` 目标文件里的段地址全是 `0x00000000`（相对地址），因为编译器只管单个文件，不知道最终放哪。**链接器读了 `.ld` 后，才把相对地址加上 ORIGIN 偏移变成绝对地址。** 这就是 relocatable（可重定位）→ executable（可执行）的过程。

实测对比：
```
链接前 startup.o:    .isr_vector VMA = 0x00000000   ← 地址未定
链接后 firmware.elf: .isr_vector VMA = 0x08000000   ← 绝对地址
```

**向量表和启动代码：同一个 .s 文件，不同的段**

启动文件（`startup_stm32f103.s`）里有两样东西，链接器把它们排进不同的段：

| 内容 | 段 | 链接后地址 |
|------|---|-----------|
| 向量表（16 个 `.word` = 16×4 = 64 = **0x40** 字节） | `.isr_vector` | `0x08000000`（Flash 最开头） |
| Reset_Handler / Default_Handler（代码） | `.text` | `0x08000040`（紧跟向量表之后） |

`.text` 从 `0x08000040` 开始，正好因为向量表占了 `0x40` 字节。如果以后向量表加外设中断条目变长，`.text` 起始地址会自动后移——链接器会算，你不用手动对。

**链接器算出的是固定的绝对地址**

裸机场景下，链接器把每个符号的地址**写死**。同样的源码 + 同样的 `.ld`，编 100 次结果完全一样：
```
main           → 0x08000094  (Flash)
cnt            → 0x20000000  (RAM)
Reset_Handler  → 0x08000040  (Flash)
```
上电、复位、跑多少次——都不影响。（对比 PC 上 Linux 程序有 ASLR 每次随机化——裸机没这套。）

**地址怎么算的？依据三样东西：**

| 依据 | 来自 | 例子 |
|------|------|------|
| 起始地址 ORIGIN | `.ld` 的 `MEMORY` | RAM 从 `0x20000000` 起 |
| 段排列顺序 | `.ld` 的 `SECTIONS` | 先 `.data` 后 `.bss` |
| 各段大小 + 对齐 | 你的代码有多少东西 | `.data` = 4 字节(一个 int) |

链接器就是"从 ORIGIN 开始、按 SECTIONS 顺序一段段往后码"：
```
RAM 示例：
0x20000000  ← ORIGIN(RAM)
   ├─ .data  (cnt, 4字节)      → cnt = 0x20000000
0x20000004  ← .data 排完接着排
   ├─ .bss   (0字节)           → _sbss = 0x20000004
   ...
0x20005000  ← ORIGIN + LENGTH(20K) = _estack (栈顶)
```

**什么时候地址会变**：只有**改代码**（加全局变量，后面整体后挪）或**改 `.ld`** 才变。

**ELF = Executable and Linkable Format（容器格式）**

ELF 是链接器产出的文件格式，不只是"一堆机器码"，而是**机器码 + 一大堆元数据**：

| 内容 | 是什么 | 用处 |
|------|--------|------|
| ELF 头 | 文件描述（Machine=ARM, Entry=Reset_Handler） | 标识目标架构和入口 |
| 各 section + 地址 | `.text`/`.data` 等段，带 VMA 和 LMA | 链接器排布的结果 |
| 符号表 (.symtab) | 名字 → 地址映射 | GDB 靠它 `break main` / `print cnt` |
| 调试信息 (.debug_*) | 源码行号、变量类型（`-g` 加的） | GDB 靠它把地址对回 `main.c` 第几行 |

**ELF vs BIN 的区别（关键）**

```
firmware.elf = 机器码 + 地址 + 符号表 + 调试信息   (胖, ~8600 字节)
firmware.bin = 只有纯机器码字节                    (瘦, ~216 字节)
```

- **`.bin`** — 给芯片吃的（纯字节），objcopy 从 ELF 抽出可烧录字节，扔掉所有元数据。
- **`.elf`** — 给人和工具吃的（带地图和说明书）。**GDB 调试必须用 ELF**，否则无法 `break main`（不知道 main 在哪）、`print cnt`（不知道地址和类型）。

> `make flash` 用的是 elf（OpenOCD 能读 elf 并按 LMA 写 Flash），`make debug` 更离不开 elf 的符号信息。

**完整心智模型**
```
源码 + .ld
   │ 链接器：按 ORIGIN + SECTIONS 顺序，算出每个符号的固定绝对地址
   ▼
firmware.elf  ← 机器码 + 地址 + 符号 + 调试信息（完整地图）
   │ objcopy：抽出纯字节
   ▼
firmware.bin  ← 只有字节
   │ OpenOCD 按 LMA 烧进 Flash
   ▼
芯片 Flash    ← 上电后硬件从这里读，地址和链接器算的完全一致
```

#### 深入笔记 · `KEEP` 与 `--gc-sections`：为什么向量表需要保护

`--gc-sections` 的规则很简单：**从入口点出发，凡是没有任何符号引用到的 section 就删掉。**
配合 `-ffunction-sections -fdata-sections`（每个函数 / 变量单独一个 section），
未调用的 HAL 函数就是这么被丢掉的 —— 这是 HAL 工程 `text` 只有 4.6K 而不是整个 HAL 库大小的原因。

但有些东西**没有任何代码引用，却必须存在**。链接器看不见「硬件会去读它」这件事。
`KEEP()` 就是告诉链接器：这个 section 不许删，哪怕没人引用。

**最典型的例子就是向量表：**

```ld
.isr_vector :
{
    . = ALIGN(4);
    KEEP(*(.isr_vector))     /* ← 没有这行，--gc-sections 会把向量表删掉 */
    . = ALIGN(4);
} > FLASH
```

全项目**没有一行 C 代码引用向量表** —— 只有硬件上电时去读 `0x08000000` / `0x08000004`
拿 SP 和 PC（见第 1 层）。在链接器眼里，这就是一坨没人用的数据。
删掉 → Flash 开头变成别的东西 → 硬件拿垃圾当 SP/PC → **芯片压根起不来**。

**需要 `KEEP` 的典型场景：**

| 场景 | 谁在"引用"它 | 为什么链接器看不见 |
|---|---|---|
| 向量表 `.isr_vector` | 硬件上电和中断时读 | 硬件行为，不是符号引用 |
| `.init_array` / `.fini_array` | `__libc_init_array` | 通过边界符号算范围，不是直接引用条目 |
| `.init` / `.fini`（`_init` 的收尾半截） | 无 | 纯收尾指令，没有符号指向它 |
| Flash 固定位置的配置数据 / bootloader 魔数 | 外部工具或另一个程序 | 根本不在本程序里 |

> **这个坑真踩过**：`STM32HAL` 的链接脚本最初漏了 `KEEP(*(.init))`，
> `_init` 的收尾指令被当垃圾回收，函数执行完不返回、顺势往下掉 → HardFault。
> 完整过程见 `STM32HAL/DEBUG_NOTES.md` 问题 3。
>
> 反过来看，本项目两个链接脚本的 `.isr_vector` 都写了 `KEEP` —— 现在你知道那行不是装饰。

### 第 4 层：构建系统

- **Make / Makefile** — 目标、依赖、规则、变量、`.PHONY`
- **（进阶）CMake** — 大项目和 IDE 集成更好
- **增量编译原理** — 为什么改一个文件不用全部重编

### 第 5 层：烧录与调试

- **OpenOCD** — 开源片上调试器，SWD/JTAG 协议桥梁
- **GDB 远程调试** — `target remote`、断点、单步、看内存/寄存器
- **SWD 协议** — ARM 两线调试接口原理
- **调试实战** — 看 HardFault、查栈、读外设寄存器状态
- ⚠️ **排查方向的直觉** — 错误可能在三步之前（链接期），`Verified OK` 不代表产物是对的。
  见 `STM32HAL/DEBUG_NOTES.md` 开头的「核心直觉」

### 第 6 层：Linux / WSL2 开发环境

- **usbipd** — WSL2 下把 USB 设备（ST-Link）透传进来
- **串口通信** — `screen`/`minicom` 看 UART 输出
- **udev 规则** — 让普通用户免 sudo 访问 ST-Link

---

## 建议的学习路径

由上而下 + 动手验证，别一上来啃理论：

1. 先让 `make flash` 把灯点亮 —— 建立正反馈，确认环境通
2. 用 `objdump` 反汇编看生成了什么 —— 把 C 和机器码对上号
3. 逐行读 `.ld` 和 `.s`，改一改看会怎样 —— 理解启动和内存布局
4. 用 GDB 单步跑一遍 `Reset_Handler` —— 亲眼看 `.data` 拷贝、`.bss` 清零
5. 加外设：UART 打印 → 定时器 → 中断 —— 逐个外设啃数据手册

---

## 立刻能做的小实验

都能在当前项目上直接跑：

```bash
# 看编译器把 main.c 变成了什么汇编
arm-none-eabi-objdump -d build/firmware.elf

# 看各个段的大小和地址
arm-none-eabi-objdump -h build/firmware.elf

# 看向量表前几个字（确认栈指针和复位向量地址）
arm-none-eabi-objdump -s -j .isr_vector build/firmware.elf
```

---

## 常用命令速查

```bash
make            # 编译，生成 build/firmware.{elf,bin,map}
make clean      # 清理
make flash      # 用 OpenOCD + ST-Link 烧录
make debug-server  # 启动 OpenOCD GDB 服务器（终端 1）
make debug      # GDB 连接调试（终端 2）
```

---

## 详细笔记

### 第 4 层 · 构建系统（Make）

Make 的意义：把冗长的编译命令固化，并且**只重编改动过的文件**。

**核心模型：规则三元组**
```make
目标(target) : 依赖(prerequisites)
	命令(recipe)        # 必须用 Tab 缩进
```
含义：要生成`目标`需要先有`依赖`；依赖比目标新就跑`命令`重建。

**增量编译**：靠文件**修改时间戳**判断谁要重建。改了 `main.c` → 只重编 `main.o` 并重新链接；`startup.s` 没动就不重编。

**自动变量**（少写文件名）：

| 符号 | 代表 |
|------|------|
| `$@` | 目标（@ 像靶心） |
| `$<` | 第一个依赖 |
| `$^` | 全部依赖（链接时收集所有 .o） |

**`| build` 竖线** = order-only 依赖：目录存在即可，不看它的时间戳（否则目录一变会害所有 .o 重编）。

**`.PHONY`** = 声明 `clean`/`flash` 这类"动作型"目标，不对应真实文件，每次都执行。

> 结论：知道"三元组 + 时间戳增量编译"即可，不需要会手写复杂 Makefile。进阶方向：`-MMD` 自动依赖生成、CMake/Meson。

### 第 5 层 · 烧录与调试（OpenOCD + GDB）

**整条链路的角色分工**：
```
电脑(WSL2)                              STM32
 GDB  ◄──►  OpenOCD ──USB──► ST-Link ──SWD(2线)──► Cortex-M3
(调试器)   (翻译官)         (硬件探针)  SWDIO+SWCLK   +调试单元(DAP)
```

| 角色 | 职责 |
|------|------|
| GDB | 下断点 / 单步 / 看变量，但不懂 STM32 |
| OpenOCD | 把 GDB 通用命令翻译成 STM32 操作，开 :3333 GDB 端口 |
| ST-Link | USB 信号 ↔ SWD 电信号，真正碰芯片引脚 |
| SWD | ARM 两线调试协议：SWDIO(数据) + SWCLK(时钟) |
| DAP | 芯片内固化的调试单元，可暂停 CPU、读写内存/寄存器、写 Flash |

**关键理解**：GDB 不直接和芯片说话，是 GDB → OpenOCD → ST-Link → SWD → 芯片，一层套一层。烧录和调试**共用同一条链路**，只是目的不同（一次性写入 vs 交互式暂停/单步）。

OpenOCD 两个 `-f`：`interface/stlink.cfg`（探针）+ `target/stm32f1x.cfg`（芯片）。换探针改前者，换芯片改后者。

**WSL2 特有关卡**：USB 透传（usbipd）。本机 `lsusb` 已能看到 `0483:3748 ST-LINK/V2`，说明透传已就绪。

**连接测试结果（真实）**：
```
STLINK V2J47S7 (API v2) VID:PID 0483:3748     ← 探针识别
Target voltage: 3.216920                       ← 板子已供电
[stm32f1x.cpu] Cortex-M3 r1p1 processor detected  ← SWD 摸到内核
target has 6 breakpoints, 4 watchpoints
```

**烧录结果（真实，`make flash`）**：
```
xPSR: 0x01000000  pc: 0x08000040  msp: 0x20005000
device id = 0x00006410
** Programming Finished **
** Verified OK **
** Resetting Target **
```
> **首尾呼应**：复位后读到的 `pc = 0x08000040`（Reset_Handler）、`msp = 0x20005000`（_estack），
> 和第 1 层从向量表字节纯手算的结果**完全一致**。从"纸上推地址"到"芯片实测"闭环。

> ⚠️ **但要记住 `Verified OK` 的边界**：它只证明「Flash 里的字节 == ELF 里的字节」，
> **不证明 ELF 本身是对的**。链接期的错误会带着 `Verified OK` 一路混过去，
> 到运行期才以 HardFault 的形式爆出来。
> 这是调试嵌入式最该有的直觉，详见 `STM32HAL/DEBUG_NOTES.md` 开头的「核心直觉」。

**GDB 调试流程（两个终端）**：
```bash
# 终端1：OpenOCD 常驻当 GDB 服务器（停在 Listening on port 3333，别关）
make debug-server

# 终端2：GDB 连上，自动停在 main
make debug
```

**GDB 常用命令**：

| 命令 | 作用 |
|------|------|
| `break main` | 下断点 |
| `continue` / `c` | 继续运行 |
| `next` / `n` | 单步（不进函数） |
| `step` / `s` | 单步（进函数） |
| `print cnt` | 看变量 |
| `info registers` | 看 CPU 寄存器 |
| `x/1xw 0x4001100C` | 读 GPIOC->ODR，单步前后各读一次可见 PC13 位翻转 |
| `x/1xw 0x40011000` | 读 GPIOC->CRL |
| `monitor reset halt` | 复位并暂停（monitor 后接发给 OpenOCD 的原始命令） |

> **重点体验**：单步走过 `GPIOC->ODR ^= (1U<<13)` 前后各读一次 ODR，亲眼看到 bit 13 翻转
> —— 第 1 层"寄存器就是内存"当场具象化。

---

## 全链路复习：启动文件 + 链接脚本，从编译到运行

### 一句话概括职责

```
链接脚本（.ld）：编译期用。告诉链接器"每个段放哪个地址"，并生成边界符号。
                 本身不含任何可执行代码。

启动文件（.s）：运行期用。含向量表（给硬件查表）+ Reset_Handler（给 CPU 执行）。
                 依赖链接脚本生成的边界符号来完成拷贝和清零。
```

两者是**配套的**：启动文件的代码引用链接脚本定义的符号，链接脚本的段布局要和启动文件的 section 名对得上。

---

### 阶段 1：编译 —— 源文件 → 可重定位目标文件（.o）

```
main.c          → arm-none-eabi-gcc -c → main.o
startup_xxx.s   → arm-none-eabi-gcc -c → startup_xxx.o
hal_gpio.c      → arm-none-eabi-gcc -c → hal_gpio.o
```

- 每个 `.o` 里的地址都是**从 0x00000000 开始的相对地址**，互相不知道对方存在。
- 此时**启动文件和链接脚本都还没发挥作用**——编译器只是逐个翻译文件。
- 汇编器不认识"向量表"这个概念，它只看 section 名（`.isr_vector` / `.text`）。

### 阶段 2：链接 —— 所有 .o → 一个 .elf

```bash
arm-none-eabi-gcc -T STM32F103C8.ld  main.o startup_xxx.o hal_gpio.o ... -o firmware.elf
```

链接器读**链接脚本**，做三件事：

| 做什么 | 具体 |
|--------|------|
| **排布地址** | 把所有 .o 里同名段合并，按脚本分配绝对地址（VMA 和 LMA） |
| **解析符号** | `main.o` 调了 `HAL_GPIO_Init`，链接器去 `hal_gpio.o` 找到地址填进去 |
| **生成边界符号** | `_sdata` / `_edata` / `_sidata` / `__init_array_start` 等，给启动代码用 |

链接完的 `.elf` 里每条指令都有了**最终绝对地址**：

```
.isr_vector  → 0x08000000              （向量表）
.text        → 0x08000000 + 向量表大小  （代码）
.rodata      → 紧跟 .text              （只读数据）
.data 的 LMA → 紧跟 .rodata            （初始值存 Flash）
.data 的 VMA → 0x20000000 起           （运行时在 RAM）
.bss  的 VMA → 紧跟 .data 的 VMA        （RAM 里，不占 Flash）
```

### 阶段 3：烧录 —— .elf → Flash

```bash
openocd ... -c "program firmware.elf verify reset exit"
```

OpenOCD 读 `.elf` 里的 **LMA 信息**，把字节写到 Flash 的物理地址上。烧完后 Flash：

```
0x08000000: [向量表][代码][只读数据][.data的初始值]
```

`.bss` **不占 Flash**——初始值就是 0，没必要存。

### 阶段 4：上电运行 —— 硬件 → 启动代码 → main

```
硬件上电
  ↓
从 0x08000000 读第 1 个字 → 写入 SP（栈顶指针）
从 0x08000004 读第 2 个字 → 写入 PC（跳到 Reset_Handler）
  ↓
Reset_Handler 开始执行（启动文件里的代码）：
  ① SystemInit()          配置 Flash 预取等基本设置
  ② 拷贝 .data            用 _sidata/_sdata/_edata 把初始值从 Flash 搬到 RAM
  ③ 清零 .bss             用 _sbss/_ebss 把这段 RAM 填 0
  ④ __libc_init_array()   遍历 .init_array 段调用里面的函数指针
  ⑤ main()                你的代码开始跑
```

---

### 易错点澄清

**① 向量表存的是 VMA，不是 LMA**

向量表里存的是**运行时地址**（VMA）——CPU 中断时直接从表里取地址跳过去执行。

对代码段（.text）来说 VMA == LMA（XIP，代码在 Flash 里原地跑），所以看起来像 LMA，但本质是 VMA。如果把代码拷到 RAM 里跑，向量表里存的就是 RAM 地址。

**② 链接脚本同时定义 VMA 和 LMA**

| 链接脚本负责什么 | 概念 | 例子 |
|---|---|---|
| 段**运行时在哪** | VMA | `.text > FLASH` → 在 0x08000000 执行 |
| 段**烧录时存哪** | LMA | `.data > RAM AT> FLASH` → 烧在 Flash，运行在 RAM |

大多数段 VMA == LMA。只有 `.data` 特殊，所以 Reset_Handler 要做一次拷贝。

**③ 向量表的三个细节**

- **第一项不是函数地址**，是栈顶值 `_estack`，硬件直接装进 SP
- **中间有保留项，值填 0**（对应的中断硬件不会产生）
- **存的地址带 Thumb 位（+1）**：`.word Reset_Handler` 实际存 `0x08000041`。CPU 取出后剥离最低位送进 xPSR 的 T 位，真正跳转用偶地址

**④ 启动文件里三块内容，各有各的"使用者"**

| 内容 | 谁调用它 | 什么时候 |
|------|---------|---------|
| **向量表**（数据） | 硬件自动读 | 上电/复位 + 中断发生时 |
| **Reset_Handler**（代码） | 硬件通过向量表第 2 项跳入 | 上电/复位后，`main` 之前 |
| **Default_Handler**（代码） | 向量表里未实现的中断条目指向它 | 中断触发但你没写 handler 时 |

**⑤ weak 符号机制：为什么 main.c 里写个 `SysTick_Handler` 就能被自动调用**

官方启动文件把所有未实现的 handler 用 weak 符号指向 `Default_Handler`（死循环）：
```asm
Default_Handler:
    b .                                            /* 死循环 */
    .weak      USART1_IRQHandler
    .thumb_set USART1_IRQHandler, Default_Handler  /* 没实现就跳死循环 */
```
你在 C 里写同名函数 → **强符号覆盖 weak 符号** → 向量表那一项指向你的函数。不是魔法。

**推论**：使能了某个中断但忘了写 handler，程序会卡在 `Default_Handler` 死循环里。用 OpenOCD `halt` 读 PC 就能区分。

**⑥ 地址的特殊含义来自硬件，不是编译器**

编译器不知道 `0x08000000` 有什么特别。是**硬件**规定上电从这里读向量表，编译器只是按链接脚本把数据放到了硬件期望的位置。

**⑦ 手写启动文件不调 `__libc_init_array` 为什么也能跑**

裸机项目手写的启动文件里根本没有 `bl __libc_init_array`，所以它压根没执行。

这个函数干两件事：遍历 `.init_array` / `.preinit_array` 里的函数指针，以及**无条件调用 `_init`**。

> **纠正一个常见误解**：很多资料说「纯 C 项目 `.init_array` 是空的」。**不对。**
> 本项目实测该段有 4 字节 = 1 个条目，内容 `0x08000175` = `frame_dummy` + Thumb 位，
> 由 `crtbegin.o` 提供（做栈回溯帧信息注册）。C++ 全局对象构造函数和
> `__attribute__((constructor))` 只是**额外**往里加条目，不是唯一来源。

换成官方启动文件后就不行了 —— 它明确写了 `bl __libc_init_array`，而链接脚本必须配套提供对应的段。

**HAL 工程实际崩在哪**（实测复现，不是推测）：

| | 遍历 array | `bl _init` |
|---|---|---|
| 缺段时符号值 | 四个边界符号全解析成 **0**（newlib 声明为 weak，未定义即 0） | — |
| 实际行为 | `0 - 0 = 0` → 跳过循环，**一次都没进** | **无条件执行** |

所以**不是「边界符号是垃圾值导致乱跳」** —— 那是静默失效。真正崩的是 `_init`：它的函数体由
`crti.o`（开头 `push`）+ `crtn.o`（结尾 `pop` / `bx lr`）拼成，缺 `KEEP(*(.init))` 时
`--gc-sections` 把收尾那半截回收了，`_init` 执行完不返回、顺势往下掉 → HardFault。

详细踩坑过程见 `STM32HAL/DEBUG_NOTES.md` 问题 3。

**⑧ 官方文件 vs 自己写**

工程项目用官方的；自己写只为学原理。

- **启动文件**：优先用官方（完整中断向量），基本不需要改
- **链接脚本**：可以自己写，但必须和启动文件对齐

以后真正会改链接脚本的场景：Bootloader + App 分区（改 `ORIGIN`）、保留一段 Flash 存配置、把关键函数放 RAM 里跑、换芯片型号（改 `LENGTH`）、外扩 SRAM。都是改现成脚本的几行，不是从零写。

**官方 `.ld` 去哪里拿** —— 不用下载，本地 SDK 里就有 212 个：

```bash
find ~/STMPrj/STM32CubeF1 -name '*.ld' | wc -l          # 212
```

最贴近本板子的参考（F103RB 和 F103C8 同属**中容量**系列，同内核同外设，只差 Flash 大小和封装）：

```
~/STMPrj/STM32CubeF1/Projects/STM32F103RB-Nucleo/Templates_LL/
    SW4STM32/NUCLEO-F103RB/STM32F103RBTx_FLASH.ld
```

拿它当参考时唯一必须改的是 `MEMORY` 里的 `LENGTH = 128K` → `64K`。
其他来源：STM32CubeMX / CubeIDE 新建工程时自动生成、或 GitHub 上 `STMicroelectronics/STM32CubeF1` 仓库同路径。

> 详细踩坑记录见 `STM32HAL/DEBUG_NOTES.md`。

---

## 学习进度

按知识层逐个攻克，学完一部分打勾：

- [x] 第 0 层：C 底层基础（指针 / 位运算 / volatile）
- [x] 第 1 层：硬件与启动流程（向量表 / Thumb位 / Reset_Handler 三步 / crt0 概念）
- [x] 第 2 层：GNU 工具链（编译四阶段 + 交叉编译 + relocatable/executable + flag + newlib-nano）
- [x] 第 3 层：链接脚本与内存布局（MEMORY/SECTIONS + 四段 + LMA/VMA + s/e/i 符号）
- [x] 第 4 层：构建系统（目标:依赖:命令 三元组 + 时间戳增量编译，会用即可）
- [x] 第 5 层：烧录与调试（链路分工 + SWD/DAP + 已实测烧录成功 / GDB 单步待动手）
- [ ] 第 6 层：Linux/WSL2 环境（usbipd + 串口 + udev）
- [ ] **中断机制（下一步）** —— 七步路线见根目录 **`INTERRUPT_ROADMAP.md`**
  - [ ] 扩展手写向量表（验证 `.text` 起始地址后移）
  - [ ] EXTI + NVIC 点亮第一个中断
  - [ ] 异常栈帧：硬件自动压的 8 个字
  - [ ] 优先级与抢占（F103 只有 4 bit 有效）
  - [ ] 原子性与临界区（`volatile` 不够用的地方）
  - [ ] HardFault 深度分析（`CFSR` / `BFAR` + 栈帧回溯）
- [ ] 外设实战：定时器 / DMA（中断之后）
