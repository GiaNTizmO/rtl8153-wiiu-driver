/* Position-independent bytes copied into one NET local-heap page and entered
 * once by the experimental entry diagnostic. No stack, no calls, no IOS state.
 * r0 = &status.observed_pc (two words), r1 = nonce. Returns nonce ^ RTLX.
 * The executing address is published so a run elsewhere cannot pass. */
.syntax unified
.cpu arm926ej-s
.arm
.section .rodata.rtl_heap_entry,"a"
.align 2
.global rtl_heap_entry_blob
.type rtl_heap_entry_blob, %object
rtl_heap_entry_blob:
    mov r3, pc          /* address of this instruction + 8 */
    ldr r2, 1f
    str r3, [r0]
    str r2, [r0, #4]
    eor r0, r1, r2
    bx lr
1:  .word 0x52544c58    /* RTLX */
.global rtl_heap_entry_blob_end
rtl_heap_entry_blob_end:
.size rtl_heap_entry_blob, rtl_heap_entry_blob_end-rtl_heap_entry_blob
.section .note.GNU-stack,"",%progbits
