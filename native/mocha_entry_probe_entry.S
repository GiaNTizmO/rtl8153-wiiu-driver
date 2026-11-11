/* Local experimental custom-Mocha entry candidate only; no console installation.
 * Same seven-instruction shape as the console-run allocation shim. */
.syntax unified
.cpu arm926ej-s
.arm
.text
.align 2
.global rtl_mocha_net_init_entry_probe
.type rtl_mocha_net_init_entry_probe, %function
rtl_mocha_net_init_entry_probe:
    /* 24 bytes preserves the incoming SP modulo 8 for the firmware call.
     * This no-argument target uses no caller-stack arguments. */
    push {r4-r8, lr}
    bl rtl_net_original_init
    mov r4, sp
    bic sp, sp, #7
    bl rtl_mocha_net_entry_probe_after_init
    mov sp, r4
    pop {r4-r8, pc}
.size rtl_mocha_net_init_entry_probe, .-rtl_mocha_net_init_entry_probe
.section .note.GNU-stack,"",%progbits
