/* KengaOS aarch64 — таблица векторов исключений (VBAR_EL1).
 *
 * 16 слотов по 128 байт. В слоте — ТОЛЬКО переход на общий обработчик
 * (полное сохранение не влезает в 128 байт). Тела обработчиков лежат
 * после таблицы: сохраняют фрейм (раскладка a64_frame в intr_a64.c),
 * зовут C-функцию, восстанавливают, eret.
 *
 * AArch64-аналог кругосветки INT3 из x86: BRK ловится как восстановимая
 * ловушка (elr += 4), остальное — паника (intr_a64.c).
 */

.section .text.vectors, "ax"
.balign 2048

.global k_a64_vectors
.type k_a64_vectors, @function
k_a64_vectors:

.macro VEC target
    .balign 128
    b   \target
.endm

VEC k_a64_sync_entry        /*  0: current EL SP0 sync   */
VEC k_a64_irq_entry         /*  1: current EL SP0 IRQ    */
VEC k_a64_dead_entry        /*  2: current EL SP0 FIQ    */
VEC k_a64_dead_entry        /*  3: current EL SP0 SError */
VEC k_a64_sync_entry        /*  4: current EL SPx sync   */
VEC k_a64_irq_entry         /*  5: current EL SPx IRQ    */
VEC k_a64_dead_entry        /*  6: current EL SPx FIQ    */
VEC k_a64_dead_entry        /*  7: current EL SPx SError */
VEC k_a64_dead_entry        /*  8: lower EL A64 sync     */
VEC k_a64_dead_entry        /*  9: lower EL A64 IRQ      */
VEC k_a64_dead_entry        /* 10: lower EL A64 FIQ      */
VEC k_a64_dead_entry        /* 11: lower EL A64 SError   */
VEC k_a64_dead_entry        /* 12: lower EL A32 sync     */
VEC k_a64_dead_entry        /* 13: lower EL A32 IRQ      */
VEC k_a64_dead_entry        /* 14: lower EL A32 FIQ      */
VEC k_a64_dead_entry        /* 15: lower EL A32 SError   */

/* --- тела обработчиков (после таблицы) ---
   Фрейм — ровно struct a64_frame в intr_a64.c:
     rsvd[2] @ 0..15, x[i] @ 16 + 8*i (x30 @ 256),
     elr @ 264, spsr @ 272, esr @ 280, far @ 288. Полезная нагрузка 296. */

.macro SAVE_FRAME
    sub     sp, sp, #304
    stp     x0, x1,  [sp, #16]
    stp     x2, x3,  [sp, #32]
    stp     x4, x5,  [sp, #48]
    stp     x6, x7,  [sp, #64]
    stp     x8, x9,  [sp, #80]
    stp     x10, x11, [sp, #96]
    stp     x12, x13, [sp, #112]
    stp     x14, x15, [sp, #128]
    stp     x16, x17, [sp, #144]
    stp     x18, x19, [sp, #160]
    stp     x20, x21, [sp, #176]
    stp     x22, x23, [sp, #192]
    stp     x24, x25, [sp, #208]
    stp     x26, x27, [sp, #224]
    stp     x28, x29, [sp, #240]
    str     x30, [sp, #256]
    mrs     x1, elr_el1
    mrs     x2, spsr_el1
    mrs     x3, esr_el1
    mrs     x4, far_el1
    stp     x1, x2, [sp, #264]
    stp     x3, x4, [sp, #280]
    mov     x0, sp
.endm

.macro RESTORE_FRAME
    ldp     x1, x2, [sp, #264]
    msr     elr_el1, x1
    msr     spsr_el1, x2
    ldp     x0, x1,  [sp, #16]
    ldp     x2, x3,  [sp, #32]
    ldp     x4, x5,  [sp, #48]
    ldp     x6, x7,  [sp, #64]
    ldp     x8, x9,  [sp, #80]
    ldp     x10, x11, [sp, #96]
    ldp     x12, x13, [sp, #112]
    ldp     x14, x15, [sp, #128]
    ldp     x16, x17, [sp, #144]
    ldp     x18, x19, [sp, #160]
    ldp     x20, x21, [sp, #176]
    ldp     x22, x23, [sp, #192]
    ldp     x24, x25, [sp, #208]
    ldp     x26, x27, [sp, #224]
    ldp     x28, x29, [sp, #240]
    ldr     x30, [sp, #256]
    add     sp, sp, #304
    eret
.endm

.balign 8
.global k_a64_sync_entry
.type k_a64_sync_entry, @function
k_a64_sync_entry:
    SAVE_FRAME
    bl      k_a64_sync_c
    RESTORE_FRAME

.balign 8
.global k_a64_irq_entry
.type k_a64_irq_entry, @function
k_a64_irq_entry:
    SAVE_FRAME
    bl      k_a64_irq_c
    RESTORE_FRAME

.balign 8
.global k_a64_dead_entry
.type k_a64_dead_entry, @function
k_a64_dead_entry:
    SAVE_FRAME
    bl      k_a64_dead_c
    RESTORE_FRAME
