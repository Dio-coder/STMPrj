# WSL2 clangd 配置（STM32 交叉编译）

在新的 WSL2 里让 VS Code 的代码跳转 / 补全 / 诊断正常工作所需的全部步骤。

**目标环境**：Ubuntu 22.04 LTS (jammy) on WSL2 + VS Code Remote-WSL
**适用工程**：`STM32HAL`（HAL 库）、`STMTest`（裸机），编译器 `arm-none-eabi-gcc`

> 前提：`SETUP.md` 第一节的工具链已装好（`arm-none-eabi-gcc`、`make`、`bear`）。
> 本文档的每条结论都在 2026-07-30 的实机上验证过，版本号是实测值。

---

## 零、这份配置要解决的核心问题

clangd 本质是个 **x86 Linux 的 C/C++ 分析器**，它默认不知道 ARM 交叉编译器的头文件在哪。
交叉编译项目要让它工作，必须解决三件事：

| 问题 | 解决手段 |
|---|---|
| clangd 怎么知道每个文件的编译参数？ | `compile_commands.json`（用 `bear` 生成，第二节） |
| clangd 怎么知道 `arm-none-eabi-gcc` 的内建头文件在哪？ | 仓库根目录的 **`.clangd`**（第五节）—— **最容易漏的一环** |
| **不止 VS Code 在用 clangd**，怎么让所有客户端都配好？ | 同上。`.clangd` 对所有客户端生效；VS Code 设置只管 VS Code（第四节） |

> 第三条容易想不到：Claude Code 的 `clangd-lsp` 插件、命令行 `clangd --check`
> 都**不读** VS Code 的设置。所以配置应该优先放在 `.clangd` 里。

---

## 一、装 clangd

```bash
sudo apt install -y clangd
```

**就这一条，没有后续步骤。**

`clangd` 是元包，依赖 `clangd-14` 并**自己提供 `/usr/bin/clangd`**（软链到 `clangd-14`）。
扩展的 `clangd.path` 默认值就是 `"clangd"`，走 PATH 查找，所以**不需要配任何路径**。

验证：

```bash
clangd --version    # 期望 Ubuntu clangd version 14.0.0-1ubuntu1.1
```

### 不要用这两种方式装

| 方式 | 为什么不要 |
|---|---|
| 弹窗「Would you like to download and install clangd X?」 | 扩展会把下载路径**回写进 User 设置**（`ConfigurationTarget.Global`）。那是机器专属的绝对路径，换机器 / 换 Windows 用户就失效，而且会污染跨环境共享的配置层。见文末附录一。 |
| `sudo apt install clangd-15` | 这个包**只装 `/usr/bin/clangd-15`，不提供 `/usr/bin/clangd`**，还得额外 `update-alternatives` 补软链接。元包 `clangd` 没这个麻烦。 |

### 想装最新版？没必要

apt 的 clangd 14 是 2022 年的，`apt.llvm.org` 能装到 22。但**升级解决不了任何本项目的问题** ——
第四节那条 `--query-driver` 在 14 和 22 上都是必需的（实测见附录二）。
纯 C11 裸机 + HAL 用不到新版 clangd 的任何特性，**停在 14**。

---

## 二、生成 `compile_commands.json`

每个工程各跑一次，在**工程目录内**：

```bash
cd ~/STMPrj/STM32HAL && bear -- make clean all
cd ~/STMPrj/STMTest  && bear -- make clean all
```

> 必须是 `make clean all` 而不是 `make` —— `bear` 靠拦截实际的编译器调用来记录参数，
> 如果 `make` 判定无需重编，什么都不会被记录，生成的数据库是空的。

### 从旧环境拷过来的 `compile_commands.json` 必须重新生成

里面记的是**绝对路径**（旧机器的用户名、旧的工具链版本目录）。直接用会让 clangd
按失效路径找头文件。检查办法：

```bash
grep -o '"directory": "[^"]*"' compile_commands.json | sort -u
```

输出必须指向当前机器的真实路径。

---

## 三、VS Code 扩展装在 WSL 侧

装 **clangd**（`llvm-vs-code-extensions.vscode-clangd`）。

关键：Remote-WSL 下扩展分两侧安装，语言服务器类扩展**必须装在 WSL 侧**。
在扩展面板里，如果按钮显示 "Install in WSL: Ubuntu-22.04"，说明当前只装在了 Windows 侧，要点它。

验证装对了：

```bash
ls ~/.vscode-server/extensions/ | grep clangd
# 期望 llvm-vs-code-extensions.vscode-clangd-0.6.0
```

> **不要同时装微软的 C/C++ 系扩展**（`ms-vscode.cpptools`、`ms-vscode.cpp-devtools`）。
> 两套都提供诊断和跳转，会互相打架，症状是重复的错误提示或跳转到错误位置。

---

## 四、写 WSL Remote 设置（关键一步）

新建 `~/.vscode-server/data/Machine/settings.json`：

```jsonc
{
  // WSL 侧专属设置（Remote 作用域），覆盖 Windows User 设置。
  // 机器相关的路径只写在这里，不要写进 User settings。
  "clangd.arguments": [
    // 授权 clangd 执行交叉编译器问出其内建头文件路径和目标三元组。
    // 不加这条，<string.h> 等非 freestanding 头文件会报 file not found。
    "--query-driver=/usr/bin/arm-none-eabi-*",
    // 全项目后台索引，跨文件跳转 / 查找引用才准。
    "--background-index",
    // 裸机项目不需要自动插入 #include。
    "--header-insertion=never"
  ]
}
```

目录可能不存在，先建：

```bash
mkdir -p ~/.vscode-server/data/Machine
```

改完 **`Ctrl+Shift+P` → `Developer: Reload Window`** 生效。

### `--query-driver` 到底在干什么

它**授权** clangd 去实际执行一次 `arm-none-eabi-gcc`，把编译器的内建搜索路径和目标三元组问出来。
生效后 clangd 日志（输出面板 → clangd）里会有：

```
got includes: "/usr/lib/gcc/arm-none-eabi/10.3.1/include,
               /usr/lib/gcc/arm-none-eabi/10.3.1/include-fixed,
               /usr/lib/gcc/arm-none-eabi/10.3.1/../../../arm-none-eabi/include"
got target:   "arm-none-eabi"
```

**不加会怎样** —— 症状很具体，不是满屏报错：

| 头文件 | 不加 query-driver | 原因 |
|---|---|---|
| `<stdint.h>` `<stddef.h>` | ✅ 正常 | freestanding 头文件，**clang 自己带一份** |
| `<string.h>` `<stdio.h>` | ❌ `file not found` | 在 newlib 的 `/usr/lib/arm-none-eabi/include/`，clang 默认不搜这个目录 |

所以 `STM32HAL/src/main.c`（第 13 行引了 `<string.h>`）会报一条错，
而 `STMTest`（不引任何非 freestanding 头）任何配置下都正常。
**只有一条红波浪线，很容易忽略过去** —— 别因为「看起来大体能用」就以为配对了。

### 为什么用通配符 `arm-none-eabi-*`

不写死 `arm-none-eabi-gcc`：这样 `g++`、`as` 之类一并授权，且路径不含工具链版本号，
以后升级 GCC 不用改这里。

### 更好的办法：用仓库里的 `.clangd` 代替（见第五节）

`--query-driver` 本身确实**只能作为命令行参数传** —— clangd 的 `.clangd` 配置文件没有
对应的配置键（能管的是 `CompileFlags`、`Diagnostics`、`Index` 等）。

**但可以绕过去**：把 query-driver 会问出来的路径，用 `CompileFlags.Add` 直接写进 `.clangd`，
效果等同，而且跟着仓库走、对所有客户端生效。**本项目已经这么做了，见第五节。**

这一节的 VS Code 设置现在是**冗余的双保险** —— 留着无害（路径重复不会出问题），
但即使删掉，靠 `.clangd` 也能正常工作。

---

## 五、项目内的 `.clangd`（推荐，一次覆盖所有客户端）

第四节的 VS Code 设置只对 VS Code 生效。但用 clangd 的**不止 VS Code**：

| 客户端 | 谁在用 | 会读 VS Code 设置吗 |
|---|---|---|
| VS Code 的 clangd 扩展 | 你，编辑器里跳转/补全 | ✅ |
| **Claude Code 的 `clangd-lsp` 插件** | Claude Code 分析你的代码 | ❌ **不会** |
| 命令行 `clangd --check` | 手动排查 | ❌ 每次要手敲参数 |

Claude Code 的插件目录里只有 README 和 LICENSE，**启动参数是内建的，无法确认它带不带
`--query-driver`**。实测证实隐患真实存在：

```
不加 --query-driver → E[...] 'string.h' file not found     1 errors
```

### 解决办法：`.clangd` 放在仓库根目录

`--query-driver` 的作用是**让 clangd 执行一次编译器、问出它的内建头文件路径**。
既然那些路径是确定的，直接写进 `.clangd` 就行 —— 效果等同，且所有客户端都吃这个文件。

本项目的 `/home/kolt/STMPrj/.clangd`：

```yaml
CompileFlags:
  Add:
    - -I/usr/lib/gcc/arm-none-eabi/10.3.1/include
    - -I/usr/lib/gcc/arm-none-eabi/10.3.1/include-fixed
    - -I/usr/lib/arm-none-eabi/include
```

这三条就是 `--query-driver='/usr/bin/arm-none-eabi-*'` 实测问出来的结果：

```
got includes: ".../10.3.1/include, .../10.3.1/include-fixed, .../arm-none-eabi/include"
```

### 实测效果

| 工程 | 只有 `.clangd`（不加 query-driver） | `.clangd` + query-driver |
|---|---|---|
| `STM32HAL` | **0 errors** | 0 errors |
| `STMTest` | **0 errors** | 0 errors |

**两者并存不冲突** —— 路径重复无害，等于双保险。

### 三个好处

1. **对所有 clangd 客户端生效** —— Claude Code 插件、VS Code、命令行 `--check`
2. **跟着仓库走** —— 换机器 clone 下来就有，不依赖任何人的 `settings.json`
3. **不会被 Settings Sync 之类的机制搞坏**（附录一那个坑的根源）

### 唯一的代价

**写死了 GCC 版本号 `10.3.1`。** 升级工具链后要改这一行，查当前值：

```bash
arm-none-eabi-gcc -print-file-name=include
```

> 相比之下 `--query-driver` 用通配符 `arm-none-eabi-*` 不含版本号，升级工具链不用改 ——
> 这是它相对 `.clangd` 唯一的优势。两个都留着就同时拿到「覆盖全客户端」和「版本无关」。

---

## 六、验证

命令行直接验证，不用等 VS Code 起来：

```bash
cd ~/STMPrj/STM32HAL
clangd --check=src/main.c --compile-commands-dir=. --query-driver='/usr/bin/arm-none-eabi-*'
```

期望结尾：

```
All checks completed, 0 errors
```

两个工程都跑一遍。`STMTest` 换成 `cd ~/STMPrj/STMTest` 即可。

> `--check` 模式**不读 VS Code 设置**，所以这里要手动把 `--query-driver` 带上。
> 这也是个好处：能把「clangd 本身能不能解析」和「VS Code 设置有没有生效」分开排查。

VS Code 侧的验证：打开 `STM32HAL/src/main.c`，第 13 行 `#include <string.h>` 不该有红波浪线，
且 `HAL_GPIO_WritePin` 之类能 `F12` 跳转到 HAL 源码。

---

## 常见卡点速查

| 现象 | 原因 |
|---|---|
| `The '...\clangd.exe' language server was not found on your PATH` | User 设置里有坏的 `clangd.path`（Windows 绝对路径）。删掉它，见附录一 |
| 弹窗一直问要不要下载 clangd | WSL 里没装 clangd。`sudo apt install -y clangd`，**别点弹窗** |
| `'string.h' file not found` | 缺交叉编译器的内建头文件路径。优先检查根目录 `.clangd` 是否存在（第五节），其次 `--query-driver`（第四节） |
| VS Code 里正常，但 Claude Code / 命令行报 `'string.h' file not found` | 只配了 VS Code 设置，没配 `.clangd`。见**第五节** |
| 升级工具链后突然报 `'string.h' file not found` | `.clangd` 里写死的 GCC 版本号过期了。用 `arm-none-eabi-gcc -print-file-name=include` 查新路径 |
| `'stdint.h' file not found` | 这个不该是 query-driver 的问题。检查 `compile_commands.json` 是否指向失效路径 |
| 头文件全找不到 / 完全没有诊断 | `compile_commands.json` 不存在或为空。用 `bear -- make clean all` 重新生成 |
| 跳转到错误位置 / 重复的错误提示 | 同时装了微软 C/C++ 扩展，禁用它 |
| 改了 Machine 设置没反应 | 要 `Developer: Reload Window`，光重启 language server 不够 |
| 跨文件「查找引用」结果不全 | `--background-index` 没开，或索引还没建完（大项目要等一会） |
| 换机器后突然全坏 | User 设置里混进了机器专属路径，见附录一 |

---

## 附录一：为什么机器专属路径不能写进 User 设置

VS Code 的设置分层，**高的覆盖低的**：

| 层 | 文件位置 | 作用范围 |
|---|---|---|
| Default | 扩展内置 | — |
| **User** | Windows: `%APPDATA%\Code\User\settings.json` | **所有环境共享，包括所有远程** |
| **Remote (Machine)** | WSL: `~/.vscode-server/data/Machine/settings.json` | 仅该远程，覆盖 User |
| Workspace | `.vscode/settings.json` | 仅该工作区 |

User 层跨远程共享是**设计如此** —— 主题、字号、`formatOnSave` 这些偏好当然希望跟着走。
但「某个二进制在哪」是机器相关的，写进 User 层就会跟着配置文件旅行到别的机器上失效。

`clangd.path` 的 scope 声明是 **`machine-overridable`**，意思就是「每台机器可以各自覆盖」——
扩展作者明确设计了隔离机制，用 Remote 层就对了。

**规则：凡是「这台机器上某个东西在哪」的设置，都不该写进 User 层。**

### 已经踩了怎么修

在 VS Code 里 `Ctrl+Shift+P` → `Preferences: Open User Settings (JSON)`，删掉这类行：

```jsonc
// ✗ 删掉
"clangd.path": "c:\\Users\\某个用户\\AppData\\Roaming\\Code\\User\\globalStorage\\...\\clangd.exe",
```

顺手检查还有没有别的失效绝对路径：

```bash
grep -nE '[a-zA-Z]:\\\\|/home/' /mnt/c/Users/*/AppData/Roaming/Code/User/settings.json
```

> 用通配符而不是 `$(whoami)` —— **WSL 用户名和 Windows 用户名通常不一样**
> （本机是 WSL `kolt` / Windows `89480`），拼出来的路径会是错的。

> 从旧环境整份拷 `settings.json` 时最容易带进来这类残留。
> 注意 Settings Sync **不会**同步 `machine` / `machine-overridable` 作用域的设置，
> 所以如果出现了别的机器的路径，来源基本是手动拷贝，不是同步。

---

## 附录二：升级 clangd 解决不了 query-driver 问题

实测矩阵（2026-07-30，`STM32HAL/src/main.c`）：

| clangd 版本 | `--query-driver` | 结果 |
|---|---|---|
| 14.0.0（apt） | ❌ | 1 错误：`'string.h' file not found` |
| 22.1.6（官方 release） | ❌ | 2 错误：同样失败 + IncludeCleaner 连带噪音 |
| 14.0.0（apt） | ✅ | **0 错误** |
| 22.1.6（官方 release） | ✅ | **0 错误** |

结论：**与版本完全无关**。别再花时间尝试「装个新版看能不能自动识别」。

反过来说，如果哪天发现旧环境「好像没这个问题」，大概率是
① 只打开过不引 `<string.h>` 的文件（如 `STMTest`），或
② 那一条红波浪线被忽略了。

---

## 附：清理从 Windows 拷过来的残留

从 Windows 拷贝工程时会带进两类垃圾：

```bash
# NTFS 备用数据流（MOTW 标记）被摊平成的独立文件
find ~/STMPrj -name '*:Zone.Identifier' -delete

# 旧环境的 clangd 索引缓存（版本不匹配，clangd 会自动重建）
rm -rf ~/STMPrj/*/.cache
```

两类都已在 `.gitignore` 里（`*:Zone.Identifier`、`.cache/`），不会进仓库。

> 避免 `Zone.Identifier` 的办法：Windows 上下载后先右键 → 属性 → 勾「解除锁定」，
> 或 PowerShell `Unblock-File`；更省事的是直接在 WSL 里 `git clone`，不经过 Windows。
