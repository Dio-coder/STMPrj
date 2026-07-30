/*
 * Startup file for STM32F103C8T6 (Cortex-M3)
 *
 * This file does three things:
 *   1. Defines the vector table (first thing in Flash)
 *   2. Implements Reset_Handler (the real entry point)
 *   3. Provides default handlers for all exceptions/interrupts
 *
 * Boot sequence:
 *   Power on → CPU reads vector_table[0] as MSP (stack pointer)
 *            → CPU reads vector_table[1] as Reset_Handler address
 *            → Jumps to Reset_Handler
 *            → Reset_Handler copies .data, zeros .bss, calls main()
 */

    .syntax unified          /* Use unified ARM/Thumb syntax */
    .cpu cortex-m3
    .thumb                   /* Generate Thumb instructions */

/* ==================== Vector Table ==================== */

    .section .isr_vector, "a", %progbits
    .type    vector_table, %object

vector_table:
    .word _estack            /* 0x00: Initial stack pointer (top of RAM) */
    .word Reset_Handler      /* 0x04: Reset */
    .word NMI_Handler        /* 0x08: Non-maskable interrupt */
    .word HardFault_Handler  /* 0x0C: Hard fault */
    .word MemManage_Handler  /* 0x10: Memory management fault */
    .word BusFault_Handler   /* 0x14: Bus fault */
    .word UsageFault_Handler /* 0x18: Usage fault */
    .word 0                  /* 0x1C: Reserved */
    .word 0                  /* 0x20: Reserved */
    .word 0                  /* 0x24: Reserved */
    .word 0                  /* 0x28: Reserved */
    .word SVC_Handler        /* 0x2C: Supervisor call */
    .word 0                  /* 0x30: Reserved */
    .word 0                  /* 0x34: Reserved */
    .word PendSV_Handler     /* 0x38: Pendable service request */
    .word SysTick_Handler    /* 0x3C: System tick timer */

    .size vector_table, . - vector_table

/* ==================== Reset Handler ==================== */

    .section .text
    .type    Reset_Handler, %function
    .global  Reset_Handler

Reset_Handler:
    /* Step 1: Copy .data section from Flash (LMA) to RAM (VMA) */
    ldr r0, =_sdata          /* r0 = RAM destination start */
    ldr r1, =_edata          /* r1 = RAM destination end */
    ldr r2, =_sidata         /* r2 = Flash source start */

copy_data:
    cmp r0, r1               /* Reached end? */
    bge zero_bss             /* Yes → go to next step */
    ldr r3, [r2], #4         /* Load word from Flash, post-increment */
    str r3, [r0], #4         /* Store to RAM, post-increment */
    b   copy_data

    /* Step 2: Zero out .bss section in RAM */
zero_bss:
    ldr r0, =_sbss           /* r0 = BSS start */
    ldr r1, =_ebss           /* r1 = BSS end */
    movs r2, #0              /* r2 = 0 (fill value) */

zero_loop:
    cmp r0, r1
    bge call_main
    str r2, [r0], #4
    b   zero_loop

    /* Step 3: Call main() */
call_main:
    bl  main                  /* Branch with link to main */
    b   .                     /* If main returns, hang here forever */

    .size Reset_Handler, . - Reset_Handler

/* ==================== Default Handlers ==================== */
/*
 * All exception handlers are defined as weak aliases to Default_Handler.
 * This means: if you don't define your own HardFault_Handler in C,
 * it falls through to an infinite loop. If you do define one, yours wins.
 */

    .type Default_Handler, %function
Default_Handler:
    b .                       /* Infinite loop */
    .size Default_Handler, . - Default_Handler

    .weak NMI_Handler
    .thumb_set NMI_Handler, Default_Handler
    .weak HardFault_Handler
    .thumb_set HardFault_Handler, Default_Handler
    .weak MemManage_Handler
    .thumb_set MemManage_Handler, Default_Handler
    .weak BusFault_Handler
    .thumb_set BusFault_Handler, Default_Handler
    .weak UsageFault_Handler
    .thumb_set UsageFault_Handler, Default_Handler
    .weak SVC_Handler
    .thumb_set SVC_Handler, Default_Handler
    .weak PendSV_Handler
    .thumb_set PendSV_Handler, Default_Handler
    .weak SysTick_Handler
    .thumb_set SysTick_Handler, Default_Handler
