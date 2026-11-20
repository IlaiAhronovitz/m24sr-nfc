/* startup.s - STM32F103 boot: core vectors, SysTick, and the C runtime. */
    .syntax unified
    .cpu cortex-m3
    .thumb

    .section .vectors, "a", %progbits
    .align 2
    .global g_vectors
g_vectors:
    .word _estack               /*  0: initial MSP   */
    .word Reset_Handler         /*  1: reset         */
    .word Fault_Handler         /*  2: NMI           */
    .word Fault_Handler         /*  3: HardFault     */
    .word Fault_Handler         /*  4: MemManage     */
    .word Fault_Handler         /*  5: BusFault      */
    .word Fault_Handler         /*  6: UsageFault    */
    .word 0, 0, 0, 0            /*  7-10: reserved   */
    .word Fault_Handler         /* 11: SVCall        */
    .word Fault_Handler         /* 12: DebugMon      */
    .word 0                     /* 13: reserved      */
    .word Fault_Handler         /* 14: PendSV        */
    .word SysTick_Handler       /* 15: SysTick       */

    .text
    .thumb_func
    .global Reset_Handler
Reset_Handler:
    ldr   r0, =_estack
    msr   msp, r0

    ldr   r0, =_sdata           /* .data: flash -> RAM */
    ldr   r1, =_edata
    ldr   r2, =_sidata
1:  cmp   r0, r1
    ittt  lo
    ldrlo r3, [r2], #4
    strlo r3, [r0], #4
    blo   1b

    ldr   r0, =_sbss            /* .bss: zero */
    ldr   r1, =_ebss
    movs  r2, #0
2:  cmp   r0, r1
    itt   lo
    strlo r2, [r0], #4
    blo   2b

    bl    main
3:  b     3b

    .thumb_func
    .global Fault_Handler
Fault_Handler:
    tst   lr, #4
    ite   eq
    mrseq r0, msp
    mrsne r0, psp
    b     fault_report
