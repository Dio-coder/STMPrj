/*
 * Linker script for STM32F103C8T6
 *
 * Flash: 64K  @ 0x08000000
 * RAM:   20K  @ 0x20000000
 *
 * This file tells the linker:
 *   1. How much memory we have and where it is
 *   2. Where to put each section (.text, .data, .bss)
 *   3. Where the stack starts
 */

/* Entry point: the symbol the CPU jumps to after reading the vector table */
ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 64K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}

/* Top of stack = end of RAM (stack grows downward on Cortex-M) */
_estack = ORIGIN(RAM) + LENGTH(RAM);   /* 0x20005000 */

SECTIONS
{
    /* --- .isr_vector: interrupt vector table, must be at flash start --- */
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    /* --- .text: executable code --- */
    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        . = ALIGN(4);
    } > FLASH

    /* --- .rodata: read-only data (const, string literals) --- */
    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    /*
     * --- .data: initialized global/static variables ---
     *
     * Initial values are stored in Flash (LMA = load address),
     * but at runtime they live in RAM (VMA = virtual address).
     * The startup code copies them from Flash to RAM.
     */
    _sidata = LOADADDR(.data);   /* Flash address where .data content starts */

    .data :
    {
        . = ALIGN(4);
        _sdata = .;              /* RAM start of .data */
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;              /* RAM end of .data */
    } > RAM AT> FLASH

    /* --- .bss: uninitialized globals (zeroed by startup code) --- */
    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM
}
