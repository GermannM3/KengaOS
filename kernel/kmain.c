/*  KengaOS — kmain.c
    Точка входа после загрузки. Лимин передаёт boot_info в rdi.
    Порядок инициализации:
      1. UART  — чтобы можно было отлаживать
      2. i18n  — установить русский по умолчанию
      3. Framebuffer — графический вывод
      4. Buddy allocator — управление физической памятью
      5. GDT, IDT, PIT — прерывания
      6. Клавиатура — интерактивность
      7. Scheduler — потоки
      8. Запуск shell
*/
#include "lib/types.h"
static const char CRLFSTR[3] = {13, 10, 0};
#include "lib/libc.h"
#include "arch/x86_64/limine.h"
#include "arch/x86_64/gdt.h"
#include "arch/x86_64/idt.h"
#include "arch/x86_64/syscall.h"
#include "drivers/uart.h"
#include "drivers/fb.h"
#include "drivers/pit.h"
#include "drivers/kbd.h"
#include "drivers/ps2mouse.h"
#include "i18n/i18n.h"
#include "ui/desktop.h"
#include "drivers/pci.h"
#include "drivers/ahci.h"
#include "drivers/pioide.h"
#include "fs/fat32.h"
#include "mem/buddy.h"
#include "vmm.h"
#include "sched/thread.h"
#include "sched/scheduler.h"
#include "fs/vfs.h"
#include "sync/sync.h"
#include "user.h"

static u64 mem_usable_kb = 0;
static u64 mem_total_kb = 0;

static void detect_memory(void) {
    struct limine_memmap_response *mm = limine_memmap_request.response;
    if (!mm) return;
    for (u64 i = 0; i < mm->entry_count; i++) {
        struct limine_memmap_entry *e = mm->entries[i];
        mem_total_kb += e->length / 1024;
        if (e->type == LIMINE_MEMMAP_USABLE) {
            mem_usable_kb += e->length / 1024;
        }
    }
}

static void boot_banner(void) {
    fb_set_text_color(FB_COLOR_CYAN, FB_COLOR_BLACK);
    fb_puts("╔════════════════════════════════════════════╗\r\n");
    fb_puts("║                                            ║\r\n");
    fb_puts("║   ██╗  ██╗██████╗ ███████╗██╗  ██╗ █████╗  ║\r\n");
    fb_puts("║   ██║ ██╔╝██╔══██╗██╔════╝██║ ██╔╝██╔══██╗ ║\r\n");
    fb_puts("║   █████╔╝ ██████╔╝█████╗  █████╔╝ ███████╗ ║\r\n");
    fb_puts("║   ██╔═██╗ ██╔══██╗██╔══╝  ██╔═██╗ ██╔══██╗ ║\r\n");
    fb_puts("║   ██║  ██╗██║  ██║███████╗██║  ██╗██║  ██║ ║\r\n");
    fb_puts("║   ╚═╝  ╚═╝╚═╝  ╚═╝╚══════╝╚═╝  ╚═╝╚═╝  ╚═╝ ║\r\n");
    fb_puts("║                                            ║\r\n");
    fb_puts("║        О С Ъ   ·   Kenga Operating System  ║\r\n");
    fb_puts("║                                            ║\r\n");
    fb_puts("╚════════════════════════════════════════════╝\r\n\r\n");

    fb_set_text_color(FB_COLOR_LIGHT_GREY, FB_COLOR_BLACK);
}

static void boot_log(void) {
    struct limine_bootloader_info_response *bi = limine_bootloader_info_request.response;

    kprintf(i18n_str(STR_BOOT_STARTING));
    kprintf(i18n_str(STR_BOOT_VERSION), KENGAOS_VERSION);
    kprintf(i18n_str(STR_BOOT_CODENAME), KENGAOS_CODENAME);
    if (bi) {
        kprintf(i18n_str(STR_BOOT_LOADED_BY), bi->name, bi->version);
    }
    kprintf(i18n_str(STR_BOOT_MEMORY), mem_usable_kb);
}

static void kernel_panic(const char *msg) {
    fb_set_text_color(FB_COLOR_WHITE, FB_COLOR_RED);
    kprintf(i18n_str(STR_PANIC), msg);
    for (;;) { __asm__ volatile ("cli; hlt"); }
}

/* ============================================================
   Демо-поток
   ============================================================ */
static volatile u64 thread_counter_1 = 0;
static volatile u64 thread_counter_2 = 0;

static void demo_thread_1(void *arg) {
    (void)arg;
    for (int i = 0; i < 50; i++) {
        thread_counter_1++;
        /* Имитация работы. */
        for (volatile int j = 0; j < 100000; j++);
    }
    /* По завершении — выходим. */
    thread_exit();
}

static void demo_thread_2(void *arg) {
    (void)arg;
    for (int i = 0; i < 50; i++) {
        thread_counter_2++;
        for (volatile int j = 0; j < 100000; j++);
    }
    thread_exit();
}

/* ============================================================
   Shell
   ============================================================ */

#define CMD_MAX_LEN 256
static char cmd_buf[CMD_MAX_LEN];
static u32  cmd_len = 0;

static void shell_print_prompt(void) {
    fb_set_text_color(FB_COLOR_GREEN, FB_COLOR_BLACK);
    fb_puts(i18n_str(STR_SHELL_PROMPT));
    fb_set_text_color(FB_COLOR_LIGHT_GREY, FB_COLOR_BLACK);
}

static void shell_print_welcome(void) {
    fb_puts(i18n_str(STR_SHELL_WELCOME));
    fb_putc('\n');
}

static void shell_print_help(void) {
    fb_puts(i18n_str(STR_SHELL_HELP_HEADER));
    fb_puts(i18n_str(STR_SHELL_CMD_HELP));
    fb_puts(i18n_str(STR_SHELL_CMD_LANG));
    fb_puts(i18n_str(STR_SHELL_CMD_INFO));
    fb_puts(i18n_str(STR_SHELL_CMD_REBOOT));
    fb_puts(i18n_str(STR_SHELL_CMD_LS));
    fb_puts(i18n_str(STR_SHELL_CMD_CAT));
    fb_puts(i18n_str(STR_SHELL_CMD_LAYOUT));
    fb_puts("  threads    — список потоков\r\n");
    fb_puts("  mem        — статистика памяти\r\n");
    fb_puts("  spawn      — запустить 2 демо-потока\r\n");
    fb_puts("  exec <file> — запустить user-программу (ring 3)\r\n");
}

static void shell_cmd_info(void) {
    kprintf(i18n_str(STR_INFO_OS_NAME));
    kprintf(i18n_str(STR_INFO_KERNEL), KENGAOS_VERSION, KENGAOS_CODENAME);
    kprintf(i18n_str(STR_INFO_ARCH));
    kprintf(i18n_str(STR_INFO_MEM_TOTAL), mem_total_kb);
    kprintf(i18n_str(STR_INFO_MEM_USABLE), mem_usable_kb);
    kprintf(i18n_str(STR_INFO_CURRENT_LANG), i18n_lang_self_name(i18n_get_lang()));
    kprintf("\r\n");
}

static void shell_cmd_lang(const char *arg) {
    if (!arg || kstrcmp(arg, "langs") == 0) {
        fb_puts("  ru — ");
        fb_puts(i18n_lang_self_name(LANG_RU));
        fb_puts(" (по умолчанию)\r\n");
        fb_puts("  en — ");
        fb_puts(i18n_lang_self_name(LANG_EN));
        fb_puts("\r\n");
        return;
    }
    int lang = i18n_parse_lang_code(arg);
    if (lang < 0) {
        fb_puts("Unknown language code. / Неизвестный код языка.\r\n");
        return;
    }
    i18n_set_lang((kenga_lang_t)lang);
    kprintf(i18n_str(STR_LANGUAGE_SWITCHED));
}

static void shell_cmd_reboot(void) {
    fb_puts("Перезагрузка...\r\n");
    __asm__ volatile (
        "cli\n"
        "lidt %0\n"
        "int3\n"
        :: "m"(*(struct { u16 a; u64 b; } *)0)
    );
    for (;;) { __asm__ volatile ("hlt"); }
}

static void shell_cmd_threads(void) {
    struct thread *list[THREAD_MAX];
    u64 n = thread_enumerate(list, THREAD_MAX);
    kprintf("Потоков: %llu\r\n", n);
    kprintf("  ID  | State         | Name\r\n");
    kprintf("  ----+---------------+--------\r\n");
    for (u64 i = 0; i < n; i++) {
        const char *state_str = "?";
        switch (list[i]->state) {
            case THREAD_UNUSED:   state_str = "UNUSED"; break;
            case THREAD_READY:    state_str = "READY"; break;
            case THREAD_RUNNING:  state_str = "RUNNING"; break;
            case THREAD_BLOCKED:  state_str = "BLOCKED"; break;
            case THREAD_FINISHED: state_str = "FINISHED"; break;
        }
        kprintf("  %llu | %s | %s\r\n", list[i]->id, state_str, list[i]->name);
    }
    kprintf("Счётчики: T1=%llu, T2=%llu\r\n", thread_counter_1, thread_counter_2);
}

static void shell_cmd_mem(void) {
    struct buddy_stats s;
    buddy_stats_get(&s);
    kprintf("=== Память (buddy allocator) ===\r\n");
    kprintf("Всего страниц:      %llu (%llu КБ)\r\n", s.total_pages, s.total_pages * 4);
    kprintf("Свободно страниц:   %llu (%llu КБ)\r\n", s.free_pages, s.free_pages * 4);
    kprintf("Занято страниц:     %llu (%llu КБ)\r\n", s.allocated_pages, s.allocated_pages * 4);
    kprintf("Распределение по order:\r\n");
    for (u8 o = 0; o <= MAX_ORDER; o++) {
        if (s.free_per_order[o] > 0) {
            kprintf("  order %2u (%4llu КБ): %llu блоков\r\n", o, (1ULL << o) * 4, s.free_per_order[o]);
        }
    }
}

static void shell_cmd_spawn(void) {
    struct thread *t1 = thread_create("demo-1", demo_thread_1, NULL);
    struct thread *t2 = thread_create("demo-2", demo_thread_2, NULL);
    if (t1 && t2) {
        sched_enqueue(t1);
        sched_enqueue(t2);
        kprintf("Создано 2 потока: #%llu, #%llu\r\n", t1->id, t2->id);
        kprintf("Они завершатся через ~5 сек. Используйте 'threads' для статуса.\r\n");
        sched_enable();
    } else {
        fb_puts("Не удалось создать потоки.\r\n");
    }
}

static void shell_cmd_ls(void) {
    if (vfs_count() == 0) {
        fb_puts(i18n_str(STR_SHELL_NO_INITRD));
        return;
    }
    struct vfs_file *list[VFS_MAX_FILES];
    u64 n = vfs_list(list, VFS_MAX_FILES);
    kprintf("Файлов в initrd: %llu\r\n", n);
    for (u64 i = 0; i < n; i++) {
        kprintf("  %8llu  %s\r\n", list[i]->size, list[i]->path);
    }
}

static void shell_cmd_cat(const char *path) {
    if (!path || !*path) {
        fb_puts(i18n_str(STR_SHELL_USAGE_CAT));
        return;
    }
    if (vfs_count() == 0) {
        fb_puts(i18n_str(STR_SHELL_NO_INITRD));
        return;
    }
    i64 fd = vfs_open(path);
    if (fd < 0) {
        kprintf(i18n_str(STR_SHELL_FILE_NOT_FOUND), path);
        return;
    }
    /* Читать по 256 байт и выводить */
    u8 buf[257];
    i64 n;
    u64 total = 0;
    while ((n = vfs_read(fd, buf, 256)) > 0) {
        buf[n] = 0;
        fb_puts((char*)buf);
        total += n;
        /* Ограничение: не выводить больше 16 КБ */
        if (total > 16384) {
            fb_puts("\r\n... (truncated)\r\n");
            break;
        }
    }
    if (total == 0) {
        fb_puts(i18n_str(STR_SHELL_FILE_EMPTY));
    }
    fb_putc('\n');
    vfs_close(fd);
}

static void shell_cmd_layout(void) {
    kbd_toggle_layout();
    const char *name = kbd_get_layout() == 1 ? "RU (ЙЦУКЕН)" : "US (QWERTY)";
    kprintf(i18n_str(STR_SHELL_LANG_CHANGED), name);
}

static void shell_cmd_exec(const char *path) {
    if (!path || !*path) {
        fb_puts("Использование: exec <file>\r\n");
        return;
    }
    if (vfs_count() == 0) {
        fb_puts(i18n_str(STR_SHELL_NO_INITRD));
        return;
    }
    u64 tid = user_exec(path);
    if (tid == 0) {
        kprintf("Не удалось запустить: %s\r\n", path);
        return;
    }
    kprintf("Запущен user-процесс: PID=%llu\r\n", tid);
    /* Включить планировщик — иначе поток не получит CPU. */
    sched_enable();
    /* Подождать немного. */
    for (volatile int i = 0; i < 50000000; i++);
}

static u32 strsize(const char *s2) { u32 n = 0; while (s2[n]) n++; return n; }
static const char *sktip(const char *s2) { while (*s2 == 32) s2++; while (*s2 && *s2 != 32) s2++; while (*s2 == 32) s2++; return s2; }

static void shell_process_command(void) {
    if (cmd_len == 0) return;

    char *cmd = cmd_buf;
    char *arg = NULL;
    for (u32 i = 0; i < cmd_len; i++) {
        if (cmd_buf[i] == ' ') {
            cmd_buf[i] = 0;
            arg = &cmd_buf[i + 1];
            break;
        }
    }
    cmd_buf[cmd_len] = 0;

    if (kstrcmp(cmd, "help") == 0) {
        shell_print_help();
    } else if (kstrcmp(cmd, "info") == 0) {
        shell_cmd_info();
    } else if (kstrcmp(cmd, "lang") == 0) {
        shell_cmd_lang(arg);
    } else if (kstrcmp(cmd, "reboot") == 0) {
        shell_cmd_reboot();
    } else if (kstrcmp(cmd, "threads") == 0) {
        shell_cmd_threads();
    } else if (kstrcmp(cmd, "mem") == 0) {
        shell_cmd_mem();
    } else if (kstrcmp(cmd, "spawn") == 0) {
        shell_cmd_spawn();
    } else if (kstrcmp(cmd, "ls") == 0) {
        shell_cmd_ls();
    } else if (kstrcmp(cmd, "cat") == 0) {
        shell_cmd_cat(arg);
    } else if (kstrcmp(cmd, "layout") == 0) {
        shell_cmd_layout();
    } else if (kstrcmp(cmd, "exec") == 0) {
        shell_cmd_exec(arg);
    } else if (kstrcmp(cmd, "dls") == 0) {
        u32 n = fat32_list();
        kprintf("fat32: %u файлов\r\n", n);
    } else if (kstrcmp(cmd, "dcat") == 0) {
        if (!arg) { kprintf("usage: dcat <file>\r\n"); }
        else {
            static u8 b[4096];
            i32 n = fat32_read(arg, b, sizeof(b));
            if (n < 0) kprintf("dcat: ошибка %d\r\n", n);
            else { b[n] = 0; kprintf("%s\r\n", b); }
        }
    } else if (kstrcmp(cmd, "dwrite") == 0) {
        if (!arg) { kprintf("usage: dwrite <file> <text>\r\n"); }
        else {
            i32 n = fat32_write(arg, (const u8 *)sktip(arg), strsize(sktip(arg)));
            kprintf("dwrite: %d байт\r\n", n);
        }
    } else if (cmd[0] == 0) {
        /* пустая строка — игнорировать */
    } else {
        kprintf(i18n_str(STR_SHELL_UNKNOWN_CMD), cmd);
    }
}

void shell_run(void) {
    shell_print_welcome();
    cmd_len = 0;
    shell_print_prompt();

    for (;;) {
        char c = kbd_getc();
        if (c == '\n' || c == '\r') {
            fb_putc('\n');
            shell_process_command();
            cmd_len = 0;
            shell_print_prompt();
        } else if (c == 0x08) {   /* backspace */
            if (cmd_len > 0) {
                cmd_len--;
                fb_putc(0x08);
                fb_putc(' ');
                fb_putc(0x08);
            }
        } else if ((u8)c >= 0x20) {
            /* Любой печатный символ (включая UTF-8 continuation байты). */
            if (cmd_len < CMD_MAX_LEN - 1) {
                cmd_buf[cmd_len++] = c;
                fb_putc(c);
            }
        }
    }
}

/* ============================================================
   Нативный модуль на языке Кэнга + shell для UI v2.
   Десктоп переехал в kernel/ui/desktop.c (refer/desktop.html).
   ============================================================ */

int g_disk_ok = 0;
int g_fat_ok = 0;

void shell_execute(const char *line) {
    u32 n = 0;
    while (line[n] && n < CMD_MAX_LEN - 1) { cmd_buf[n] = line[n]; n++; }
    cmd_buf[n] = 0;
    cmd_len = n;
    shell_process_command();
    cmd_len = 0;
}

/* ============================================================
   kmain — главная точка входа
   ============================================================ */
void kmain(void) {

    /* 1. UART — раньше всего */
    uart_init(UART_COM1);
    uart_puts(UART_COM1, "\r\n[KengaOS UART]\r\n");

    /* 2. i18n — по умолчанию русский */
    i18n_set_lang(LANG_RU);

    /* 3. Framebuffer */
    struct limine_framebuffer_response *fb_resp = limine_framebuffer_request.response;
    if (fb_resp && fb_resp->framebuffer_count > 0) {
        fb_init(fb_resp->framebuffers[0]);
    } else {
        uart_puts(UART_COM1, "WARNING: нет framebuffer, режим UART-only\r\n");
    }

    /* 4. Память */
    detect_memory();

    /* 5. Графический баннер */
    boot_banner();

    /* 6. Boot-лог на обоих выводах */
    boot_log();

    /* 7. GDT, IDT, PIT */
    gdt_init();
    kprintf(i18n_str(STR_BOOT_GDT_OK));
    idt_init();
    kprintf(i18n_str(STR_BOOT_IDT_OK));
    /* Включить прерывания (IRQ0 timer, IRQ1 kbd). */
    __asm__ volatile ("sti");
    pit_init(PIT_FREQUENCY);
    kprintf(i18n_str(STR_BOOT_TIMER_OK));

    /* 8. VMM (paging) — раньше buddy: тот пишет через HHDM (phys_to_virt). */
    vmm_init();
    kprintf("[OK] VMM инициализирован (paging, HHDM=0x%llx)\r\n", vmm_get_hhdm());

    /* 9. Buddy allocator */
    buddy_init(limine_memmap_request.response);
    kprintf("[OK] Buddy allocator инициализирован\r\n");

    /* 10. Scheduler */
    sched_init();
    irq_register(0, sched_tick);
    kprintf("[OK] Планировщик инициализирован (round-robin, 10 мс)\r\n");

    /* 11. Syscall */
    syscall_init();
    kprintf("[OK] Syscall (SYSCALL/SYSRET) инициализирован\r\n");

    /* 12. VFS (initrd) */
    vfs_init();
    if (vfs_count() > 0) {
        kprintf("[OK] initrd загружен: %llu файлов\r\n", vfs_count());
    } else {
        kprintf("[!] initrd не загружен (команды ls/cat/exec недоступны)\r\n");
    }

    /* 12.5 PCI + AHCI (SATA): диск и самотест R/W */
    pci_init();
    g_disk_ok = ahci_init();
    if (!g_disk_ok) g_disk_ok = pioide_init() == 1;
    uart_puts(UART_COM1, "[dw] init done\r\n");
    if (g_disk_ok) {
        static u8 wbuf[512], rbuf[512];
        for (int i = 0; i < 512; i++) wbuf[i] = (u8)(i * 7 + 3);
        int w, r;
        if (ahci_sectors()) { w = ahci_write(100, 1, wbuf); r = ahci_read(100, 1, rbuf); }
        else { w = pioide_write(100, 1, wbuf); r = pioide_read(100, 1, rbuf); }
        int same = 1;
        for (int i = 0; i < 512; i++) if (wbuf[i] != rbuf[i]) same = 0;
                kprintf("[dbg] w=");
        for (int i2 = 0; i2 < 8; i2++) kprintf("%02x", wbuf[i2]);
        kprintf(" r=");
        for (int i2 = 0; i2 < 8; i2++) kprintf("%02x", rbuf[i2]);
        kprintf("\r\n");
kprintf("[%s] disk: write=%d read=%d %s\r\n",
                (w == 0 && r == 0 && same) ? "OK" : "!!", w, r,
                same ? "verify ok" : "VERIFY FAIL");
        g_disk_ok = (w == 0 && r == 0 && same);
        {
            /* прямой тест: паттерн в LBA 5 — проверяется на хосте */
            static u8 z0[512];
            for (int i = 0; i < 512; i++) z0[i] = (u8)(0x5A + i);
            int tw = 0;
            if (ahci_sectors()) tw = ahci_write(5, 1, z0);
            else tw = pioide_write(5, 1, z0);
            kprintf("[%s] disk: lba0-write=%d\r\n", tw == 0 ? "OK" : "!!", tw);
        }
    }

    /* 12.6 FAT32 поверх диска: самотест записи в файл */
    if (g_disk_ok) {
        int fm = fat32_init();
        g_fat_ok = fm;
        if (fm) {
            static const char msg[] = "hello from kengaos disk!\r\n";
            static u8 rbuf[64];
            i32 wr = fat32_write("README.TXT", (const u8 *)msg, sizeof(msg));
            i32 rd = fat32_read("README.TXT", rbuf, sizeof(rbuf));
            int same = rd > 0 && kmemcmp(rbuf, msg, (u32)rd) == 0;
            kprintf("[%s] fat32: write=%d read=%d %s\r\n",
                    (wr > 0 && same) ? "OK" : "!!", wr, rd,
                    same ? "verify ok" : "VERIFY FAIL");
            g_fat_ok = (wr > 0 && same);
            fat32_list();
        }
    }

    /* 13. Клавиатура и мышь */
    kbd_init();
    ps2mouse_init();

    /* 14. Нативный модуль на языке Кэнга (.kenga → freestanding C).
       k_kmod_selftest читает CMOS-секунды через свои порт-интринсики;
       сверяем с чтением inb из ядра — секунда могла тикнуть, читаем до и после. */
    {
        extern long long k_kmod_selftest(void);  /* из kernel/kenga/kmod.c */
        outb(0x70, 0x00);
        unsigned char before = inb(0x71);
        long long packed = k_kmod_selftest();
        outb(0x70, 0x00);
        unsigned char after = inb(0x71);
        long long magic = (packed >> 8) & 0xFF, sec = packed & 0xFF;
        int ok = (magic == 0x5A) && (sec == before || sec == after);
        kprintf("[%s] kenga-модуль: bitops%s portio rtc=%llu%s\r\n",
                ok ? "OK" : "!!",
                magic == 0x5A ? "+" : "-",
                (unsigned long long)sec, ok ? "" : " MISMATCH");
    }

    /* 14. Готово */
    kprintf(i18n_str(STR_BOOT_DONE));
    fb_putc('\n');

    /* 15. Десктоп UI v2 (refer/desktop.html). */
    ui_desktop_run();

    kernel_panic("kmain: shell вышел");
}
