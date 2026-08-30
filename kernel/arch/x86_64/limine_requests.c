/*  KengaOS — Limine-запросы (v12 протокол).
    Все request'ы лежат в секции .limine_requests МЕЖДУ маркерами
    START и END — Limine обрабатывает только то, что между ними.
*/
#include "limine.h"

/* Маркер начала секции. */
__attribute__((used, section(".limine_requests")))
volatile u64 limine_requests_start_marker[4] = LIMINE_REQUESTS_START_MARKER;

/* Базовая ревизия протокола (запрашиваем v0 = базовая) */
__attribute__((used, section(".limine_requests")))
struct limine_base_revision limine_base_revision = {
    .base_revision = LIMINE_BASE_REVISION(0)
};

__attribute__((used, section(".limine_requests")))
struct limine_bootloader_info_request limine_bootloader_info_request = {
    .id = LIMINE_BOOTLOADER_INFO_REQUEST_ID,
    .revision = 0,
    .response = NULL
};

__attribute__((used, section(".limine_requests")))
struct limine_memmap_request limine_memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0,
    .response = NULL
};

__attribute__((used, section(".limine_requests")))
struct limine_framebuffer_request limine_framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0,
    .response = NULL
};

__attribute__((used, section(".limine_requests")))
struct limine_module_request limine_module_request = {
    .id = LIMINE_MODULE_REQUEST_ID,
    .revision = 0,
    .response = NULL,
    .internal_module_count = 0,
    .internal_modules = NULL
};

__attribute__((used, section(".limine_requests")))
struct limine_hhdm_request limine_hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0,
    .response = NULL
};

/* Маркер конца секции — должен стоять ПОСЛЕ всех запросов. */
__attribute__((used, section(".limine_requests")))
struct limine_kernel_address_request limine_kernel_address_request = {
    .id = { 0x5c110b26a3c9f7bc, 0x85544cd877c47ee1, 0x785c37ed3d8b488e, 0 },
    .revision = 0,
    .response = NULL,
};

volatile u64 limine_requests_end_marker[2] = LIMINE_REQUESTS_END_MARKER;
