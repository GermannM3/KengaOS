/*  KengaOS — тестовая user-программа hello.
    Компилируется в static ELF, загружается в initrd,
    запускается командой `exec hello.elf`.
*/
#include "userlib.h"

/* Константы для вывода UTF-8 кириллицы. */
static const char msg_en[] = "Hello from user-space!\n";
static const char msg_ru[] = "Привет из пользовательского режима!\n";

void _start(void) {
    write(1, msg_en, sizeof(msg_en) - 1);
    write(1, msg_ru, sizeof(msg_ru) - 1);

    /* Вывести PID */
    long pid = get_pid();
    char pid_str[16];
    int i = 14;
    pid_str[15] = '\n';
    if (pid == 0) {
        pid_str[i] = '0';
        i--;
    } else {
        while (pid > 0 && i >= 0) {
            pid_str[i--] = '0' + (pid % 10);
            pid /= 10;
        }
    }
    write(1, "PID: ", 5);
    write(1, &pid_str[i + 1], 15 - i);

    /* Уступить CPU */
    yield();
    write(1, "After yield.\n", 13);

    /* Завершиться */
    exit(0);
}
