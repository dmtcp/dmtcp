/* crt1_ppc64.S - Replacement for system crt1.o on PPC64 ELFv2 (ppc64le)
 * Static mtcp_restart does not use glibc startup.  Linux enters _start with
 * argc/argv/envp laid out on the initial stack, so hand that stack pointer to
 * a tiny C helper that reconstructs the arguments and calls main(). */


    .section ".text"
    .align 2
    .globl _start
    .type _start, @function
_start:
    /* Global entry point: kernel sets r12 = entry address.
     * Set up TOC pointer (r2) relative to r12. */
    addis 2, 12, .TOC.-_start@ha
    addi  2, 2,  .TOC.-_start@l
.localentry _start, .-_start

    /* Preserve the kernel-provided initial stack pointer. */
    mr    3, 1

    /* Align stack to 16 bytes and allocate frame */
    rldicr 1, 1, 0, 59
    li    0, 0
    stdu  1, -128(1)
    mtlr  0
    std   0, 0(1)

    /* mtcp_ppc64_start(initial_sp) reconstructs argc/argv/envp from stack. */
    bl    mtcp_ppc64_start
    nop

    /* Should never return */
    trap
        .size _start, .-_start

     .globl mtcp_ppc64_reset_toc
    .type mtcp_ppc64_reset_toc, @function
mtcp_ppc64_reset_toc:
    bcl   20,31,1f
1:
    mflr  12
    addis 2,12,.TOC.-1b@ha
    addi  2,2,.TOC.-1b@l
    blr
    .size mtcp_ppc64_reset_toc, .-mtcp_ppc64_reset_toc

.globl mtcp_restart_new_stack
    .type mtcp_restart_new_stack, @function
mtcp_restart_new_stack:
    bcl   20,31,1f
1:
    mflr  12
    addis 2,12,.TOC.-1b@ha
    addi  2,2,.TOC.-1b@l
    b     mtcp_restart_new_stack_body
    .size mtcp_restart_new_stack, .-mtcp_restart_new_stack
    .globl main_new_stack
    .type main_new_stack, @function
main_new_stack:
    bcl   20,31,1f
1:
    mflr  12
    addis 2,12,.TOC.-1b@ha
    addi  2,2,.TOC.-1b@l
    b     main_new_stack_body
    .size main_new_stack, .-main_new_stack

