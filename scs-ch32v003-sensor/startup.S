/* Minimal reset path: no SDK, interrupts or vendor interrupt ABI. */
.section .init, "ax", @progbits
.global _start
.type _start, @function
_start:
    j reset_handler

.section .text.reset_handler, "ax", @progbits
reset_handler:
    csrci mstatus, 8
    fence.i                      /* Synchronize the interrupt mask (WCH RM). */
    .option push
    .option norelax
    la gp, __global_pointer$
    la sp, _stack_top
    .option pop

    /* Direct trap mode: unexpected exceptions stop here. */
    la t0, trap_handler
    csrw mtvec, t0

    la t0, _data_load
    la t1, _data_start
    la t2, _data_end
1:
    bgeu t1, t2, 2f
    lw a0, 0(t0)
    sw a0, 0(t1)
    addi t0, t0, 4
    addi t1, t1, 4
    j 1b

2:
    la t0, _bss_start
    la t1, _bss_end
3:
    bgeu t0, t1, 4f
    sw zero, 0(t0)
    addi t0, t0, 4
    j 3b

4:
    fence.i                      /* Make the copied RAM wake helper executable. */
    call main
    j trap_handler

.balign 4
trap_handler:
    j trap_handler

/* WCH SDK WFE(11) equivalent for HSI=24 MHz. Execute from RAM so the
 * post-wakeup settling loop precedes any return to Flash. No IRQ enabling. */
.section .ramtext.standby_wfe, "ax", @progbits
.balign 4
.global standby_wfe
.type standby_wfe, @function
standby_wfe:
    fence iorw, iorw              /* Commit peripheral setup before sleeping. */
    li a0, 11
    wfi                          /* PFIC.WFITOWFE must be set by the caller. */
1:
    addi a0, a0, -1
    bnez a0, 1b
    ret
.size standby_wfe, .-standby_wfe
