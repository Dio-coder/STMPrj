# 编译说明

## 日常编译

```bash
make              # 增量编译
make clean all    # 清除后重编
make flash        # 烧录到板子
```

## 生成 compile_commands.json（VS Code 跳转）

```bash
# 安装 bear（只需一次）
sudo apt install bear

# 生成编译数据库
bear -- make clean all
```

生成后 VS Code 的 clangd 扩展会自动识别它。

> **但只有这一步还不够**：clangd 还需要 `--query-driver` 才能问出 `arm-none-eabi-gcc`
> 的内建头文件路径，否则 `main.c` 里的 `<string.h>` 会报 `file not found`
> （`<stdint.h>` 不受影响，clang 自带）。配置方法见根目录的 **`CLANGD_SETUP.md`**。

**什么时候需要重新生成：** 新增源文件、改了 include 路径、改了宏定义时重新跑一次 `bear -- make clean all`。日常编译不需要带 `bear`。

## 调试

```bash
# 终端 1：启动 OpenOCD 调试服务器
make debug-server

# 终端 2：启动 GDB 连接
make debug
```
