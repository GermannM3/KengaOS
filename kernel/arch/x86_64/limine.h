/*  KengaOS — Limine boot protocol v12 (актуальная версия).
    Полная спецификация: https://github.com/limine-bootloader/limine

    Структуры и magic-значения взяты из limine-12.6.0/limine-protocol/include/limine.h.
*/
#ifndef KENGA_LIMINE_H
#define KENGA_LIMINE_H

#include "../lib/types.h"

/* Общий префикс для всех request IDs (первые 2 qword из 4) */
#define LIMINE_COMMON_MAGIC 0xc7b1dd30df4c8b88, 0x0a82e883a194f07b

/* Маркеры начала/конца секции .limine_requests */
#define LIMINE_REQUESTS_START_MARKER { 0xf6b8f4b39de7d1ae, 0xfab91a6940fcb9cf, \
                                       0x785c6ed015d3e316, 0x181e920a7852b9d9 }
#define LIMINE_REQUESTS_END_MARKER   { 0xadc0e0531bb10d03, 0x9572709f31764c62 }

/* Базовая ревизия протокола (запрашиваем v0) */
#define LIMINE_BASE_REVISION(N) { 0xf9562b2d5c95a6c8, 0x6a7b384944536bdc, (N) }

/* ID'ы запросов: { common_magic_lo, common_magic_hi, request_id_lo, request_id_hi } */
#define LIMINE_BASE_REVISION_REQUEST_ID   { 0xf9562b2d5c95a6c8, 0x6a7b384944536bdc, 0 }
#define LIMINE_BOOTLOADER_INFO_REQUEST_ID { LIMINE_COMMON_MAGIC, 0xf55038d8e2a1202f, 0x279426fcf5f59740 }
#define LIMINE_MEMMAP_REQUEST_ID          { LIMINE_COMMON_MAGIC, 0x67cf3d9d378a806f, 0xe304acdfc50c3c62 }
#define LIMINE_FRAMEBUFFER_REQUEST_ID     { LIMINE_COMMON_MAGIC, 0x9d5827dcd881dd75, 0xa3148604f6fab11b }
#define LIMINE_MODULE_REQUEST_ID          { LIMINE_COMMON_MAGIC, 0x3e7e279702be32af, 0xca1c4f3bd1280cee }
#define LIMINE_HHDM_REQUEST_ID            { LIMINE_COMMON_MAGIC, 0x48dcf1cb8ad2b852, 0x63984e959a98244b }

/* Memory map types */
#define LIMINE_MEMMAP_USABLE                 0
#define LIMINE_MEMMAP_RESERVED               1
#define LIMINE_MEMMAP_ACPI_RECLAIMABLE       2
#define LIMINE_MEMMAP_ACPI_NVS               3
#define LIMINE_MEMMAP_BAD_MEMORY             4
#define LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE 5
#define LIMINE_MEMMAP_EXECUTABLE_AND_MODULES 6
#define LIMINE_MEMMAP_FRAMEBUFFER            7

/* Базовый request: 4 qword ID + revision + указатель на response */
struct limine_request {
    u64 id[4];
    u64 revision;
    void *response;
};

/* Базовая ревизия протокола (отличается от обычного request) */
struct limine_base_revision {
    u64 base_revision[3];
};

/* === Bootloader info === */
struct limine_bootloader_info_response {
    u64 revision;
    char *name;
    char *version;
};
struct limine_bootloader_info_request {
    u64 id[4];
    u64 revision;
    struct limine_bootloader_info_response *response;
};

/* === Memory map === */
struct limine_memmap_entry {
    u64 base;
    u64 length;
    u64 type;
};
/* === Kernel address (физ. база ядра — buddy не должен раздавать его память) === */
struct limine_kernel_address_response {
    u64 revision;
    u64 virtual_base;
    u64 physical_base;
};
struct limine_kernel_address_request {
    u64 id[4];
    u64 revision;
    struct limine_kernel_address_response *response;
};

struct limine_memmap_response {
    u64 revision;
    u64 entry_count;
    struct limine_memmap_entry **entries;
};
struct limine_memmap_request {
    u64 id[4];
    u64 revision;
    struct limine_memmap_response *response;
};

/* === Framebuffer === */
struct limine_framebuffer {
    void *address;
    u64 width;
    u64 height;
    u64 pitch;
    u16 bpp;
    u8  memory_model;
    u8  red_mask_size;
    u8  red_mask_shift;
    u8  green_mask_size;
    u8  green_mask_shift;
    u8  blue_mask_size;
    u8  blue_mask_shift;
    u8  reserved;
};
struct limine_framebuffer_response {
    u64 revision;
    u64 framebuffer_count;
    struct limine_framebuffer **framebuffers;
};
struct limine_framebuffer_request {
    u64 id[4];
    u64 revision;
    struct limine_framebuffer_response *response;
};

/* === Modules (initrd) === */
struct limine_file {
    u64 revision;
    void *address;
    u64 size;
    char *path;
    char *cmdline;
    u32 media_type;
    u32 unused;
    u8  tftp_ipv4[4];
    u32 tftp_port;
    u32 partition_index;
    u32 mbr_disk_id;
    u8  gpt_disk_uuid[16];
    u8  gpt_part_uuid[16];
    u8  part_uuid[16];
};
struct limine_module_response {
    u64 revision;
    u64 module_count;
    struct limine_file **modules;
};
struct limine_module_request {
    u64 id[4];
    u64 revision;
    struct limine_module_response *response;
    /* Request revision 1 — internal modules. Не используем. */
    u64 internal_module_count;
    void *internal_modules;
};

/* === HHDM (Higher Half Direct Map) ===
   Limine предоставляет прямое отображение всей физической памяти
   в высокую половину виртуального адресного пространства.
   Используется для доступа к физическим адресам (например, framebuffer). */
struct limine_hhdm_response {
    u64 revision;
    void *offset;
};
struct limine_hhdm_request {
    u64 id[4];
    u64 revision;
    struct limine_hhdm_response *response;
};

/* === Глобальные request-переменные === */
extern struct limine_base_revision              limine_base_revision;
extern struct limine_bootloader_info_request    limine_bootloader_info_request;
extern struct limine_memmap_request             limine_memmap_request;
extern struct limine_kernel_address_request    limine_kernel_address_request;
extern struct limine_framebuffer_request        limine_framebuffer_request;
extern struct limine_module_request             limine_module_request;
extern struct limine_hhdm_request               limine_hhdm_request;

#endif
