# ---- Project ----
TARGET   = firmware
BUILD    = build

# ---- Paths (HAL 库在项目外的 STM32CubeF1 里) ----
CUBE     = ../STM32CubeF1
HAL_DIR  = $(CUBE)/Drivers/STM32F1xx_HAL_Driver
CMSIS_DEV = $(CUBE)/Drivers/CMSIS/Device/ST/STM32F1xx
CMSIS_INC = $(CUBE)/Drivers/CMSIS/Include

# ---- Toolchain ----
CC       = arm-none-eabi-gcc
AS       = arm-none-eabi-gcc
OBJCOPY  = arm-none-eabi-objcopy
SIZE     = arm-none-eabi-size

# ---- MCU ----
MCU      = -mcpu=cortex-m3 -mthumb -mfloat-abi=soft

# ---- Sources ----

# 你的代码
C_SRC  = src/main.c

# HAL 库（只编译你用到的模块）
C_SRC += $(HAL_DIR)/Src/stm32f1xx_hal.c
C_SRC += $(HAL_DIR)/Src/stm32f1xx_hal_rcc.c
C_SRC += $(HAL_DIR)/Src/stm32f1xx_hal_rcc_ex.c
C_SRC += $(HAL_DIR)/Src/stm32f1xx_hal_gpio.c
C_SRC += $(HAL_DIR)/Src/stm32f1xx_hal_gpio_ex.c
C_SRC += $(HAL_DIR)/Src/stm32f1xx_hal_uart.c
C_SRC += $(HAL_DIR)/Src/stm32f1xx_hal_cortex.c
C_SRC += $(HAL_DIR)/Src/stm32f1xx_hal_dma.c
C_SRC += $(HAL_DIR)/Src/stm32f1xx_hal_flash.c
C_SRC += $(HAL_DIR)/Src/stm32f1xx_hal_flash_ex.c

# CMSIS system init
C_SRC += $(CMSIS_DEV)/Source/Templates/system_stm32f1xx.c

# 启动文件（用官方的，比你手写的多了完整的中断向量）
AS_SRC  = $(CMSIS_DEV)/Source/Templates/gcc/startup_stm32f103xb.s

# ---- Include paths ----
INCLUDES  = -I src
INCLUDES += -I $(HAL_DIR)/Inc
INCLUDES += -I $(CMSIS_DEV)/Include
INCLUDES += -I $(CMSIS_INC)

# ---- Defines ----
DEFS  = -DSTM32F103xB        # 告诉 CMSIS 头文件：我是 F103 中容量型号
DEFS += -DUSE_HAL_DRIVER      # 启用 HAL

# ---- Compiler flags ----
CFLAGS   = $(MCU) $(INCLUDES) $(DEFS)
CFLAGS  += -std=c11 -Wall
CFLAGS  += -Og -g
CFLAGS  += -ffunction-sections -fdata-sections

# ---- Assembler flags ----
ASFLAGS  = $(MCU) -g

# ---- Linker flags ----
LDFLAGS  = $(MCU)
LDFLAGS += -T STM32F103C8.ld
LDFLAGS += --specs=nosys.specs --specs=nano.specs
LDFLAGS += -Wl,--gc-sections
LDFLAGS += -Wl,-Map=$(BUILD)/$(TARGET).map

# ---- Build object list (flatten all .c/.s into build/) ----
OBJS  = $(addprefix $(BUILD)/, $(notdir $(C_SRC:.c=.o)))
OBJS += $(addprefix $(BUILD)/, $(notdir $(AS_SRC:.s=.o)))

# Tell make where to find source files
VPATH  = $(sort $(dir $(C_SRC) $(AS_SRC)))

# ---- Rules ----
all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).bin size

$(BUILD)/$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.s | $(BUILD)
	$(AS) $(ASFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

size: $(BUILD)/$(TARGET).elf
	$(SIZE) $<

flash: $(BUILD)/$(TARGET).elf
	openocd -f interface/stlink.cfg -f target/stm32f1x.cfg \
		-c "program $< verify reset exit"

debug-server:
	openocd -f interface/stlink.cfg -f target/stm32f1x.cfg

debug: $(BUILD)/$(TARGET).elf
	arm-none-eabi-gdb $< \
		-ex "target remote :3333" \
		-ex "monitor reset halt" \
		-ex "break main" \
		-ex "continue"

clean:
	rm -rf $(BUILD)

.PHONY: all clean flash debug debug-server size
