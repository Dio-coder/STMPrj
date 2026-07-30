# ---- Project config ----
TARGET   = firmware
BUILD    = build

# ---- Toolchain ----
CC       = arm-none-eabi-gcc
AS       = arm-none-eabi-gcc
LD       = arm-none-eabi-gcc
OBJCOPY  = arm-none-eabi-objcopy
SIZE     = arm-none-eabi-size

# ---- Sources ----
C_SRC    = src/main.c
AS_SRC   = src/startup_stm32f103.s

# ---- MCU flags (Cortex-M3, no FPU) ----
MCU      = -mcpu=cortex-m3 -mthumb -mfloat-abi=soft

# ---- Compiler flags ----
CFLAGS   = $(MCU)
CFLAGS  += -std=c11 -Wall -Wextra
CFLAGS  += -Og -g                    # Optimize for debug + debug symbols
CFLAGS  += -ffunction-sections       # Each function in its own section
CFLAGS  += -fdata-sections           # Each variable in its own section
CFLAGS  += -I include

# ---- Assembler flags ----
ASFLAGS  = $(MCU) -g

# ---- Linker flags ----
LDFLAGS  = $(MCU)
LDFLAGS += -T STM32F103C8.ld
LDFLAGS += --specs=nosys.specs       # Bare metal: no OS syscalls
LDFLAGS += --specs=nano.specs        # Use newlib-nano (smaller libc)
LDFLAGS += -Wl,--gc-sections         # Remove unused sections (with -ffunction-sections)
LDFLAGS += -Wl,-Map=$(BUILD)/$(TARGET).map  # Generate map file

# ---- Object files ----
OBJS     = $(BUILD)/main.o $(BUILD)/startup_stm32f103.o

# ---- Rules ----

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).bin size

$(BUILD)/$(TARGET).elf: $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/main.o: src/main.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/startup_stm32f103.o: src/startup_stm32f103.s | $(BUILD)
	$(AS) $(ASFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

size: $(BUILD)/$(TARGET).elf
	$(SIZE) $<

# ---- Flash & debug ----

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
