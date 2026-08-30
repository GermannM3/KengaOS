/*  KengaOS — таблицы строк для i18n.
    Все строки хранятся как UTF-8 литералы в rodata.
    Добавить новый язык = добавить ещё один столбец в таблицу.
*/
#include "i18n.h"

/* ============================================================
   Русский (умолчание)
   ============================================================ */
static const char *const strings_ru[STR_COUNT] = {
    [STR_BOOT_STARTING]        = "KengaOS загружается...\r\n",
    [STR_BOOT_VERSION]         = "Версия: %s\r\n",
    [STR_BOOT_CODENAME]        = "Кодовое имя: %s\r\n",
    [STR_BOOT_LOADED_BY]       = "Загружено: %s %s\r\n",
    [STR_BOOT_MEMORY]          = "Память: %llu КБ доступно\r\n",
    [STR_BOOT_FRAMEBUFFER_OK]  = "[OK] Framebuffer: %ux%u, %u bpp\r\n",
    [STR_BOOT_GDT_OK]          = "[OK] GDT загружен\r\n",
    [STR_BOOT_IDT_OK]          = "[OK] IDT загружен\r\n",
    [STR_BOOT_TIMER_OK]        = "[OK] Таймер запущен (1000 Гц)\r\n",
    [STR_BOOT_DONE]            = "KengaOS готова.\r\n",
    [STR_LANGUAGE_NAME]        = "Русский",
    [STR_LANGUAGE_SWITCHED]    = "Язык интерфейса изменён.\r\n",
    [STR_SHELL_PROMPT]         = "kenga> ",
    [STR_SHELL_WELCOME]        = "Добро пожаловать в KengaOS. Введите 'help' для списка команд.\r\n",
    [STR_SHELL_HELP_HEADER]    = "Доступные команды:\r\n",
    [STR_SHELL_CMD_HELP]       = "  help       — этот список\r\n",
    [STR_SHELL_CMD_LANG]       = "  lang <ru|en> — сменить язык интерфейса\r\n",
    [STR_SHELL_CMD_REBOOT]     = "  reboot     — перезагрузка\r\n",
    [STR_SHELL_CMD_INFO]       = "  info       — информация о системе\r\n",
    [STR_SHELL_UNKNOWN_CMD]    = "Неизвестная команда: %s\r\n",
    [STR_INFO_OS_NAME]         = "Имя ОС:        KengaOS\r\n",
    [STR_INFO_KERNEL]          = "Ядро:          %s (%s)\r\n",
    [STR_INFO_ARCH]            = "Архитектура:   x86_64\r\n",
    [STR_INFO_MEM_TOTAL]       = "Память всего:  %llu КБ\r\n",
    [STR_INFO_MEM_USABLE]      = "Память доступна: %llu КБ\r\n",
    [STR_INFO_CURRENT_LANG]    = "Язык:          %s\r\n",
    [STR_PANIC]                = "PANIC: %s\r\n",
    [STR_SHELL_CMD_LS]         = "  ls         — список файлов в initrd\r\n",
    [STR_SHELL_CMD_CAT]        = "  cat <file> — вывести содержимое файла\r\n",
    [STR_SHELL_CMD_LANG_TOGGLE]= "  layout     — переключить раскладку клавиатуры (US/RU)\r\n",
    [STR_SHELL_CMD_LAYOUT]     = "  layout     — переключить раскладку (US ↔ RU, или F12 в runtime)\r\n",
    [STR_SHELL_NO_INITRD]      = "initrd не загружен.\r\n",
    [STR_SHELL_FILE_NOT_FOUND] = "Файл не найден: %s\r\n",
    [STR_SHELL_FILE_EMPTY]     = "Файл пуст.\r\n",
    [STR_SHELL_USAGE_CAT]      = "Использование: cat <имя_файла>\r\n",
    [STR_SHELL_LANG_CHANGED]   = "Раскладка: %s (F12 — переключить)\r\n",
};

/* ============================================================
   Английский
   ============================================================ */
static const char *const strings_en[STR_COUNT] = {
    [STR_BOOT_STARTING]        = "KengaOS booting...\r\n",
    [STR_BOOT_VERSION]         = "Version: %s\r\n",
    [STR_BOOT_CODENAME]        = "Codename: %s\r\n",
    [STR_BOOT_LOADED_BY]       = "Loaded by: %s %s\r\n",
    [STR_BOOT_MEMORY]          = "Memory: %llu KB usable\r\n",
    [STR_BOOT_FRAMEBUFFER_OK]  = "[OK] Framebuffer: %ux%u, %u bpp\r\n",
    [STR_BOOT_GDT_OK]          = "[OK] GDT loaded\r\n",
    [STR_BOOT_IDT_OK]          = "[OK] IDT loaded\r\n",
    [STR_BOOT_TIMER_OK]        = "[OK] Timer started (1000 Hz)\r\n",
    [STR_BOOT_DONE]            = "KengaOS is ready.\r\n",
    [STR_LANGUAGE_NAME]        = "English",
    [STR_LANGUAGE_SWITCHED]    = "Interface language changed.\r\n",
    [STR_SHELL_PROMPT]         = "kenga> ",
    [STR_SHELL_WELCOME]        = "Welcome to KengaOS. Type 'help' for command list.\r\n",
    [STR_SHELL_HELP_HEADER]    = "Available commands:\r\n",
    [STR_SHELL_CMD_HELP]       = "  help       — this list\r\n",
    [STR_SHELL_CMD_LANG]       = "  lang <ru|en> — switch interface language\r\n",
    [STR_SHELL_CMD_REBOOT]     = "  reboot     — restart the system\r\n",
    [STR_SHELL_CMD_INFO]       = "  info       — system information\r\n",
    [STR_SHELL_UNKNOWN_CMD]    = "Unknown command: %s\r\n",
    [STR_INFO_OS_NAME]         = "OS name:       KengaOS\r\n",
    [STR_INFO_KERNEL]          = "Kernel:        %s (%s)\r\n",
    [STR_INFO_ARCH]            = "Architecture:  x86_64\r\n",
    [STR_INFO_MEM_TOTAL]       = "Total memory:  %llu KB\r\n",
    [STR_INFO_MEM_USABLE]      = "Usable memory: %llu KB\r\n",
    [STR_INFO_CURRENT_LANG]    = "Language:      %s\r\n",
    [STR_PANIC]                = "PANIC: %s\r\n",
    [STR_SHELL_CMD_LS]         = "  ls         — list files in initrd\r\n",
    [STR_SHELL_CMD_CAT]        = "  cat <file> — print file contents\r\n",
    [STR_SHELL_CMD_LANG_TOGGLE]= "  layout     — toggle keyboard layout (US/RU)\r\n",
    [STR_SHELL_CMD_LAYOUT]     = "  layout     — toggle layout (US ↔ RU, or F12 at runtime)\r\n",
    [STR_SHELL_NO_INITRD]      = "initrd not loaded.\r\n",
    [STR_SHELL_FILE_NOT_FOUND] = "File not found: %s\r\n",
    [STR_SHELL_FILE_EMPTY]     = "File is empty.\r\n",
    [STR_SHELL_USAGE_CAT]      = "Usage: cat <filename>\r\n",
    [STR_SHELL_LANG_CHANGED]   = "Layout: %s (F12 to toggle)\r\n",
};

/* Массив указателей на таблицы языков */
static const char *const *lang_tables[LANG_COUNT] = {
    [LANG_RU] = strings_ru,
    [LANG_EN] = strings_en,
};

static const char *const lang_self_names[LANG_COUNT] = {
    [LANG_RU] = "Русский",
    [LANG_EN] = "English",
};

static kenga_lang_t current_lang = LANG_RU;   /* умолчание */

kenga_lang_t i18n_set_lang(kenga_lang_t lang) {
    if (lang < 0 || lang >= LANG_COUNT) return current_lang;
    kenga_lang_t old = current_lang;
    current_lang = lang;
    return old;
}

kenga_lang_t i18n_get_lang(void) {
    return current_lang;
}

const char *i18n_str(kenga_strid_t id) {
    if (id < 0 || id >= STR_COUNT) return "<?>";
    const char *s = lang_tables[current_lang][id];
    return s ? s : "<?>";
}

const char *i18n_lang_self_name(kenga_lang_t lang) {
    if (lang < 0 || lang >= LANG_COUNT) return "<?>";
    return lang_self_names[lang];
}

int i18n_parse_lang_code(const char *code) {
    if (!code) return -1;
    /* Сравнение без strcmp (нет libc) */
    if (code[0]=='r' && code[1]=='u' && code[2]==0) return LANG_RU;
    if (code[0]=='e' && code[1]=='n' && code[2]==0) return LANG_EN;
    return -1;
}
