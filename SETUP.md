# WSL2 STM32 开发环境搭建

从零在新的 WSL2 里把这套工程跑起来所需的全部步骤。

**目标环境**：Ubuntu 22.04 LTS (jammy) on WSL2
**目标板**：STM32F103C8T6（Blue Pill）
**烧录器**：ST-Link V2

> 本文档的包名和版本号已按 **jammy** 源核对过。
> 原开发环境是 Ubuntu 24.04，两者的差异在文末「Ubuntu 22.04 vs 24.04 差异」一节列出。

---

## 一、Linux 侧：安装工具链

```bash
sudo apt update
sudo apt install -y \
    gcc-arm-none-eabi \
    binutils-arm-none-eabi \
    libnewlib-arm-none-eabi \
    libnewlib-dev \
    gdb-multiarch \
    openocd \
    make \
    git \
    bear \
    minicom
```

各包作用：

| 包 | jammy 版本 | 提供什么 | 干什么用 |
|---|---|---|---|
| `gcc-arm-none-eabi` | 15:10.3-2021.07-4 (GCC **10.3**) | `arm-none-eabi-gcc` | 交叉编译器（none=无 OS，eabi=嵌入式 ABI） |
| `binutils-arm-none-eabi` | 2.38 | `arm-none-eabi-ld` / `objcopy` / `size` / `objdump` / `nm` | 链接、格式转换、查看体积和反汇编 |
| `libnewlib-arm-none-eabi` | 3.3.0 | newlib / newlib-nano | 精简版 C 标准库（`--specs=nano.specs` 用的就是它） |
| `libnewlib-dev` | 3.3.0 | newlib 头文件 | 编译期需要 |
| `gdb-multiarch` | 12.0.90 | `gdb-multiarch` | 调试（见下方说明） |
| `openocd` | **0.11.0** | `openocd` | 烧录 + GDB 服务器 |
| `make` | 4.3 | `make` | 构建系统 |
| `bear` | 3.0.18 | `bear` | 生成 `compile_commands.json`，给 VS Code 跳转用 |
| `minicom` | 2.8 | `minicom` | 串口终端，看 UART 输出 |

> **不要加 `libstdc++-arm-none-eabi-newlib`** —— 这个包在 jammy 源里**不存在**，
> 写进 `apt install` 会让整条命令直接失败（`E: Unable to locate package`）。
> 本项目是纯 C，不需要它。

### GDB：`gdb-arm-none-eabi` 这个包不存在

jammy 和 noble 源里**都没有 `gdb-arm-none-eabi`**，只能用 `gdb-multiarch`（已包含在上面的安装命令里）。

工程 `Makefile` 的 `debug` 目标写的是 `arm-none-eabi-gdb`，所以要建个软链接才能直接用：

```bash
sudo ln -s /usr/bin/gdb-multiarch /usr/local/bin/arm-none-eabi-gdb
```

> `gdb-multiarch` 连接 OpenOCD 时会从 ELF 自动识别架构，一般不需要手动 `set architecture arm`。

### 验证安装

```bash
arm-none-eabi-gcc --version    # 期望 10.3.1
arm-none-eabi-gdb --version    # 软链接生效的话显示 GNU gdb 12.0.90
openocd --version              # 期望 0.11.0
make --version                 # 期望 4.3
bear --version                 # 期望 3.0.18
```

### 想用更新的 GCC（可选）

jammy 的 GCC 10.3 编译这个项目完全没问题，**不需要升级**。
但如果以后想用新版特性（或写 C++），apt 源满足不了，就下 ARM 官方 tarball：

```bash
# 从 developer.arm.com 下载 arm-gnu-toolchain-*-x86_64-arm-none-eabi.tar.xz
sudo tar xJf arm-gnu-toolchain-*.tar.xz -C /opt
echo 'export PATH=/opt/arm-gnu-toolchain-*/bin:$PATH' >> ~/.bashrc
```
装完记得把 apt 版本卸掉或调整 PATH 优先级，避免两套混用。

---

## 二、下载 ST 官方 SDK（HAL 库）

HAL 项目依赖 `STM32CubeF1`，放在**工程的同级目录**（`Makefile` 里写死了 `CUBE = ../STM32CubeF1`）。

```bash
mkdir -p ~/MCU_Proj && cd ~/MCU_Proj

git clone --depth 1 https://github.com/STMicroelectronics/STM32CubeF1.git
```

### 关键：HAL 驱动和 CMSIS 是 submodule，必须单独初始化

`--depth 1` 浅克隆**不会**拉 submodule，克隆完 `Drivers/STM32F1xx_HAL_Driver/` 是空目录。必须补一步：

```bash
cd ~/MCU_Proj/STM32CubeF1
git submodule update --init --depth 1 \
    Drivers/STM32F1xx_HAL_Driver \
    Drivers/CMSIS/Device/ST/STM32F1xx \
    Drivers/CMSIS
```

### 验证 SDK 完整性

下面四个路径都必须存在，缺任何一个 HAL 项目都编译不过：

```bash
cd ~/MCU_Proj/STM32CubeF1
ls Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_gpio.c
ls Drivers/STM32F1xx_HAL_Driver/Inc/stm32f1xx_hal.h
ls Drivers/CMSIS/Device/ST/STM32F1xx/Include/stm32f103xb.h
ls Drivers/CMSIS/Device/ST/STM32F1xx/Source/Templates/gcc/startup_stm32f103xb.s
ls Drivers/CMSIS/Include/core_cm3.h
```

> SDK 体积不小（含大量示例工程和文档）。只想要驱动的话可以只克隆
> `STM32CubeF1_HAL_Driver` 和 `cmsis_device_f1` 两个独立仓库，但目录结构要自己拼，
> `Makefile` 的路径也得改。**新环境建议照上面来，省事。**

---

## 三、Windows 侧：把 ST-Link 透传进 WSL2

**WSL2 默认访问不到 USB 设备**，这是新环境最容易卡住的地方。需要 `usbipd-win`。

### 1. Windows 上安装 usbipd（PowerShell 管理员）

```powershell
winget install --interactive --exact dorssel.usbipd-win
```

装完**重启一次** PowerShell（或整个终端）。

### 2. WSL2 里装 usbip 客户端

```bash
sudo apt install -y linux-tools-generic hwdata
sudo update-alternatives --install /usr/local/bin/usbip usbip \
    /usr/lib/linux-tools/*-generic/usbip 20
```

### 3. 每次插上 ST-Link 后（Windows PowerShell 管理员）

```powershell
usbipd list                    # 找到 ST-Link 的 BUSID，形如 2-4
usbipd bind   --busid 2-4      # 只需做一次，重启后保持
usbipd attach --wsl --busid 2-4    # 每次重新插拔 / 重启 WSL 后都要跑
```

### 4. WSL2 里确认

```bash
lsusb        # 应该能看到 STMicroelectronics ST-LINK
```

> **注意**：`attach` 之后 Windows 侧就用不了这个设备了（比如 STM32CubeProgrammer）。
> 用 `usbipd detach --busid 2-4` 还回去。

### udev 权限

`openocd` 包自带规则文件 `/usr/lib/udev/rules.d/60-openocd.rules`，已经覆盖 ST-Link，
**正常情况不用额外配**。如果 `openocd` 报 `LIBUSB_ERROR_ACCESS`：

```bash
sudo usermod -aG plugdev $USER      # 之后需要重开 WSL 会话生效
sudo udevadm control --reload-rules && sudo udevadm trigger
```

WSL2 里 udev 有时不会自动启动，实在不行用 `sudo openocd ...` 临时绕过
（但**不要用 `sudo make`**——会把 `build/` 目录变成 root 所有，后面普通用户编译报权限错误）。

---

## 四、串口（看 UART 输出）

用 USB-TTL 模块（CH340 / CP2102 之类），**和 ST-Link 是两个独立设备**，都要 attach。

### 接线（Blue Pill ↔ USB-TTL）

| Blue Pill | USB-TTL | 说明 |
|---|---|---|
| PA9 (USART1_TX) | RX | 交叉接 |
| PA10 (USART1_RX) | TX | 交叉接 |
| GND | GND | **必须接**，不接会乱码或完全没数据 |

> **不要接 VCC**——板子已经由 ST-Link 或 USB 供电，双路供电有风险。

### Windows 侧同样要 attach

```powershell
usbipd list                        # 找 USB-Serial 的 BUSID
usbipd bind   --busid 2-5
usbipd attach --wsl --busid 2-5
```

### WSL2 里打开串口

```bash
ls /dev/ttyUSB*                    # 确认设备节点，通常是 /dev/ttyUSB0
sudo usermod -aG dialout $USER     # 加权限，之后重开 WSL 会话
minicom -b 115200 -D /dev/ttyUSB0
```

`minicom` 退出：`Ctrl-A` 然后 `X`。

> 首次用 minicom 建议先 `minicom -s` 进配置，关掉 **Hardware Flow Control**，否则可能收不到数据。

---

## 五、VS Code 代码跳转

1. 装扩展 **clangd**（不要同时装微软的 C/C++ 扩展，两个会打架）
2. 在工程目录生成编译数据库：
   ```bash
   cd ~/MCU_Proj/STM32HAL
   bear -- make clean all
   ```
3. 重启 clangd（命令面板 → `clangd: Restart language server`）

裸机项目同理，在 `~/MCU_Proj/STMTest` 里也跑一次。

> **上面三步不够** —— 还必须配 `--query-driver`，否则 `<string.h>` 之类非 freestanding
> 的标准库头会报 `file not found`（`STM32HAL/src/main.c` 就会中招）。
> 完整步骤、踩坑速查和 VS Code 设置分层的说明见 **`CLANGD_SETUP.md`**。

详见 `STM32HAL/BUILD.md`。

---

## 六、验证整条链路

```bash
cd ~/MCU_Proj/STM32HAL

make clean all      # 看到 text / data / bss 三个数字即成功
make flash          # 期望看到 "** Verified OK **" 和 "** Resetting Target **"
```

> **体积不用和旧环境对齐**。原 Ubuntu 24.04 环境（GCC 13.2）编出来是
> `text=4696 data=20 bss=104`；GCC 10.3 编出来数字会有出入，这是正常的，
> 只要能链接成功、`text` 在 5KB 上下就对了（64K Flash 完全够）。

烧录成功后板上 PC13 的 LED 应该以约 1 秒周期闪烁（500ms 亮 500ms 灭）。

串口接好后应该看到：
```
Hello from STM32 HAL!
blink 0
blink 1
...
```

---

## 常见卡点速查

| 现象 | 原因 |
|---|---|
| `E: Unable to locate package libstdc++-arm-none-eabi-newlib` | jammy 没这个包，从安装命令里删掉 |
| `arm-none-eabi-gcc: command not found` | 工具链没装 |
| `arm-none-eabi-gdb: command not found` | apt 源没这个包，装 `gdb-multiarch` 后建软链接 |
| HAL 头文件找不到 | SDK 的 submodule 没初始化 |
| `openocd` 报 `open failed` / 找不到设备 | Windows 侧没 `usbipd attach` |
| `openocd` 报 `LIBUSB_ERROR_ACCESS` | udev 权限，加 `plugdev` 组或临时 `sudo openocd` |
| `Permission denied` 编译报错 | 之前用过 `sudo make`，`sudo rm -rf build` 后重编 |
| `/dev/ttyUSB0` 不存在 | USB-TTL 没 attach 进 WSL2 |
| 串口打开报权限错误 | 没加 `dialout` 组 |
| 串口全是乱码 | 波特率不对，或 GND 没接 |
| 烧录成功但 LED 不闪 | 见 `STM32HAL/DEBUG_NOTES.md` |

---

## Ubuntu 22.04 vs 24.04 差异

原开发环境是 24.04，迁到 22.04 需要注意的地方（版本号均已核对 apt 源）：

| 包 | jammy 22.04 | noble 24.04 | 影响 |
|---|---|---|---|
| `gcc-arm-none-eabi` | **10.3** | 13.2 | 编译产物体积会有差异，功能无影响 |
| `binutils-arm-none-eabi` | 2.38 | 2.42 | 无影响 |
| `libnewlib-arm-none-eabi` | 3.3.0 | 4.4.0 | 无影响 |
| `libstdc++-arm-none-eabi-newlib` | **不存在** | 存在 | **必须从安装命令里去掉**，否则 apt 整条失败 |
| `gdb-arm-none-eabi` | 不存在 | 不存在 | 两边都得用 `gdb-multiarch` |
| `gdb-multiarch` | 12.0.90 | 15.1 | 无影响 |
| `openocd` | **0.11.0** | 0.12.0 | 见下 |
| `bear` | 3.0.18 | 3.1.3 | 无影响 |
| `minicom` | 2.8 | 2.9 | 无影响 |

### OpenOCD 0.11 需要留意的点

工程 `Makefile` 用的是：
```makefile
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg
```
`interface/stlink.cfg` 在 0.11 里就有，**这条命令不用改**。

如果 0.11 下报找不到配置文件，先确认实际路径：
```bash
ls /usr/share/openocd/scripts/interface/ | grep -i stlink
ls /usr/share/openocd/scripts/target/ | grep -i stm32f1
```

### 项目本身不需要改动

`Makefile` / 链接脚本 / 源码在 GCC 10.3 下都能直接编译，
用的都是 C11 和标准 GNU 链接脚本语法，没有依赖新版工具链的特性。

---

## 附：本地工程文档索引

| 文档 | 内容 |
|---|---|
| `CLANGD_SETUP.md` | clangd 完整配置：`--query-driver`、VS Code 设置分层、跨机器迁移踩坑 |
| `STMTest/LEARNING.md` | 知识地图 + 分层学习笔记（向量表 / 链接脚本 / 工具链 / 烧录调试） |
| `STM32HAL/PROJECT_STRUCTURE.md` | HAL 工程结构：每个文件为什么需要、加新外设的步骤 |
| `STM32HAL/DEBUG_NOTES.md` | 三个踩坑记录 + 通用调试套路（halt → 读 PC → 定位） |
| `STM32HAL/BUILD.md` | 编译 / 烧录 / 调试命令速查 |
