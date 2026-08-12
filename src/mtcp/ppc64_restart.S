        .text
        .align 2
        .globl ppc64_restart_on_new_stack
        .type ppc64_restart_on_new_stack,@function
ppc64_restart_on_new_stack:
        mr      0, 2
        lwsync
        mr      2, 0
        mr      12, 4
        mtctr   12
        isync
        bctr
        .size ppc64_restart_on_new_stack,.-ppc64_restart_on_new_stack

