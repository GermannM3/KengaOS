/*  KengaOS — интернационализация (i18n).
    Подход как у взрослых ОС (POSIX gettext-стиль, но проще):
      - В коде используются не строки, а идентификаторы (STR_*).
      - runtime: lookup по текущему языку → UTF-8 строка.
      - Переключение языка меняет указатель таблицы — мгновенно.

    Языки:
      LANG_RU — русский (умолчание)
      LANG_EN — английский
      (расширение: добавить LANG_ES, LANG_DE, LANG_ZH просто дописав таблицу)
*/
#ifndef KENGA_I18N_H
#define KENGA_I18N_H

#include "../lib/types.h"

typedef enum {
    LANG_RU = 0,
    LANG_EN = 1,
    LANG_COUNT
} kenga_lang_t;

typedef enum {
    STR_BOOT_STARTING = 0,
    STR_BOOT_VERSION,
    STR_BOOT_CODENAME,
    STR_BOOT_LOADED_BY,
    STR_BOOT_MEMORY,
    STR_BOOT_FRAMEBUFFER_OK,
    STR_BOOT_GDT_OK,
    STR_BOOT_IDT_OK,
    STR_BOOT_TIMER_OK,
    STR_BOOT_DONE,
    STR_LANGUAGE_NAME,           /* "Русский" / "English" — самоназвание */
    STR_LANGUAGE_SWITCHED,
    STR_SHELL_PROMPT,
    STR_SHELL_WELCOME,
    STR_SHELL_HELP_HEADER,
    STR_SHELL_CMD_HELP,
    STR_SHELL_CMD_LANG,
    STR_SHELL_CMD_REBOOT,
    STR_SHELL_CMD_INFO,
    STR_SHELL_UNKNOWN_CMD,
    STR_INFO_OS_NAME,
    STR_INFO_KERNEL,
    STR_INFO_ARCH,
    STR_INFO_MEM_TOTAL,
    STR_INFO_MEM_USABLE,
    STR_INFO_CURRENT_LANG,
    STR_PANIC,
    STR_SHELL_CMD_LS,
    STR_SHELL_CMD_CAT,
    STR_SHELL_CMD_LANG_TOGGLE,
    STR_SHELL_CMD_LAYOUT,
    STR_SHELL_NO_INITRD,
    STR_SHELL_FILE_NOT_FOUND,
    STR_SHELL_FILE_EMPTY,
    STR_SHELL_USAGE_CAT,
    STR_SHELL_LANG_CHANGED,
    STR_COUNT
} kenga_strid_t;

/* Установить текущий язык. Возвращает предыдущий. */
kenga_lang_t i18n_set_lang(kenga_lang_t lang);

/* Получить текущий язык. */
kenga_lang_t i18n_get_lang(void);

/* Получить строку на текущем языке. */
const char *i18n_str(kenga_strid_t id);

/* Утилита: распарсить команду вида "ru" / "en" → enum. Возвращает -1 если не распознано. */
int i18n_parse_lang_code(const char *code);

/* Самоназвание языка (для меню переключения). */
const char *i18n_lang_self_name(kenga_lang_t lang);

#endif
