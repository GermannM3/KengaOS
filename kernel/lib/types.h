/*  KengaOS — базовые типы.
    Никаких зависимостей от libc. Только то, что ядро использует само.
*/
#ifndef KENGA_TYPES_H
#define KENGA_TYPES_H

typedef unsigned char           u8;
typedef unsigned short          u16;
typedef unsigned int            u32;
typedef unsigned long long      u64;
typedef signed char             i8;
typedef signed short            i16;
typedef signed int              i32;
typedef signed long long        i64;
typedef unsigned long           usize;
typedef unsigned long           uintptr;
typedef unsigned long           phys_t;

enum { false = 0, true = 1 };
typedef int bool;

#define NULL ((void*)0)

#define KENGAOS_VERSION "0.0.5"
#define KENGAOS_CODENAME "Заря"   /* "Dawn" — кодовое имя первого релиза */

#endif
