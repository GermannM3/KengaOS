/*  KengaOS — User process launcher.
    Запускает ELF в ring 3 через iretq.
*/
#include "user.h"
#include "elf.h"
#include "../lib/libc.h"
#include "../fs/vfs.h"
#include "../vmm/vmm.h"
#include "../mem/buddy.h"
#include "../sched/thread.h"
#include "../sched/scheduler.h"
#include "../arch/x86_64/gdt.h"
#include "../drivers/uart.h"

/* Стек ядра для user-процесса (для syscall/interrupt handlers). */
#define USER_KERNEL_STACK_PAGES 4

/* Структура потока для user-процесса — расширяем обычный thread. */
struct user_thread_info {
    u64 pml4_phys;
    u64 kernel_stack;        /* стек для syscall handler */
    u64 entry;               /* user RIP */
    u64 user_rsp;            /* user RSP */
};

#define MAX_USER_PROCS 32
static struct user_thread_info user_info[MAX_USER_PROCS];

/* Найти свободный слот user_info. */
static struct user_thread_info *user_info_alloc(void) {
    for (int i = 0; i < MAX_USER_PROCS; i++) {
        if (user_info[i].pml4_phys == 0) {
            return &user_info[i];
        }
    }
    return NULL;
}

/* ============================================================
   Точка входа в user-процесс — переключается из kernel в user.
   После vmm_switch_to и установки RSP0 в TSS, делает iretq с RPL=3.

   Параметры (передаются через структуру в памяти, т.к. iretq сложно
   с регистрами): pml4_phys, entry, user_rsp.

   Это функция, которая вызывается через context_switch из scheduler'а.
   ============================================================ */
extern void user_jump(u64 entry, u64 user_rsp, u64 pml4_phys);

/* Главная функция user-потока. */
static void user_thread_main(void *arg) {
    struct user_thread_info *info = (struct user_thread_info *)arg;

    /* Переключиться на address space процесса */
    vmm_switch_to(info->pml4_phys);

    /* Установить kernel RSP0 в TSS (для syscall handler). */
    gdt_set_rsp0(info->kernel_stack);

    /* Сохранить kernel RSP для syscall handler. */
    extern u64 kernel_rsp_storage;
    kernel_rsp_storage = info->kernel_stack;

    /* Перейти в user-mode. */
    user_jump(info->entry, info->user_rsp, info->pml4_phys);

    /* Сюда не должны дойти. */
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

u64 user_exec(const char *path) {
    /* Найти файл в VFS */
    if (vfs_count() == 0) return 0;
    struct vfs_file *file = NULL;
    for (u64 i = 0; i < vfs_count(); i++) {
        struct vfs_file *f = vfs_get(i);
        if (kstrcmp(f->path, path) == 0) {
            file = f;
            break;
        }
    }
    if (!file) return 0;

    /* Создать address space */
    u64 pml4_phys = vmm_create_address_space();
    if (pml4_phys == 0) return 0;

    /* Загрузить ELF */
    struct elf_info einfo;
    if (!elf_load(file, pml4_phys, &einfo)) {
        vmm_destroy_address_space(pml4_phys);
        return 0;
    }

    /* Аллоцировать kernel stack для syscall */
    u64 kstack_phys = buddy_alloc(2);   /* 4 pages = 16 KB */
    if (kstack_phys == 0) {
        vmm_destroy_address_space(pml4_phys);
        return 0;
    }
    /* TSS.RSP0 и syscall-стек — kernel-virtual (HHDM), не физадрес. */
    u64 kstack_top = (u64)phys_to_virt(kstack_phys) + USER_KERNEL_STACK_PAGES * 4096;

    /* Зарегистрировать user_info */
    struct user_thread_info *info = user_info_alloc();
    if (!info) {
        buddy_free(kstack_phys, 2);
        vmm_destroy_address_space(pml4_phys);
        return 0;
    }
    info->pml4_phys = pml4_phys;
    info->kernel_stack = kstack_top;
    info->entry = einfo.entry;
    info->user_rsp = einfo.stack_top - 16;   /* оставить 16 байт под fake return addr */

    /* Создать thread. */
    char name[32];
    kstrncpy(name, "user-proc", 11);
    struct thread *t = thread_create(name, user_thread_main, info);
    if (!t) {
        buddy_free(kstack_phys, 2);
        vmm_destroy_address_space(pml4_phys);
        info->pml4_phys = 0;
        return 0;
    }

    sched_enqueue(t);
    return t->id;
}
