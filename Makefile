# KengaOS — Makefile
# Сборка ядра для x86_64.
#
# Зависимости:
#   - gcc (host-gcc с -ffreestanding работает, но рекомендуется x86_64-elf-gcc)
#   - ld
#   - nasm
#   - xorriso (для ISO)
#   - Limine binary (скачивается скриптом scripts/fetch-limine.sh)

# === Toolchain ===
CC      ?= gcc
LD      ?= ld
NASM    ?= nasm
XORRISO ?= xorriso

# === Флаги компиляции ===
ARCH_DIR = arch/x86_64

CFLAGS  = -ffreestanding -fno-stack-protector -fno-stack-check -fno-pie -fno-pic \
	  -fno-omit-frame-pointer -m64 -march=x86-64 -mno-red-zone -mno-mmx \
	  -mno-sse -mno-sse2 -mcmodel=kernel \
	  -Wall -Wextra -Wno-unused-parameter -g -O2 \
	  -Ikernel -Ikernel/lib -Ikernel/arch/x86_64 -Ikernel/drivers -Ikernel/i18n \
	  -Ikernel/mem -Ikernel/sched -Ikernel/fs -Ikernel/sync -Ikernel/vmm -Ikernel/user -Ikernel/ui -Ifonts

LDFLAGS = -nostdlib -static -z max-page-size=0x1000 -T kernel/arch/x86_64/linker.ld -m elf_x86_64

# === Исходники ===
C_SOURCES = \
	kernel/kmain.c \
	kernel/lib/libc.c \
	kernel/i18n/i18n.c \
	kernel/arch/x86_64/limine_requests.c \
	kernel/arch/x86_64/gdt.c \
	kernel/arch/x86_64/idt.c \
	kernel/arch/x86_64/syscall.c \
	kernel/drivers/fb.c \
	kernel/drivers/pit.c \
	kernel/drivers/kbd.c \
	kernel/drivers/ps2mouse.c \
	kernel/drivers/pci.c \
	kernel/drivers/ahci.c \
	kernel/drivers/pioide.c \
	kernel/fs/vfs.c \
	kernel/fs/fat32.c \
	kernel/sync/sync.c \
	kernel/vmm/vmm.c \
	kernel/user/elf.c \
	kernel/user/user.c \
	kernel/mem/buddy.c \
	kernel/sched/thread.c \
	kernel/sched/scheduler.c \
	kernel/kenga/kmod.c \
	kernel/ui/desktop.c \
	fonts/font.c \
	fonts/ui_font.c

ASM_SOURCES = \
	kernel/arch/x86_64/entry.S \
	kernel/arch/x86_64/isr_asm.S \
	kernel/arch/x86_64/switch.S \
	kernel/arch/x86_64/syscall_asm.S \
	kernel/arch/x86_64/user_jump.S

# === Промежуточные объектники ===
OBJDIR = build/obj
OBJECTS = $(patsubst %.c,$(OBJDIR)/%.o,$(C_SOURCES)) \
	  $(patsubst %.S,$(OBJDIR)/%.o,$(ASM_SOURCES))

# === Мост kenga-lang → ядро: .kenga → freestanding C ===
KENGA ?= D:/KengaOS/kenga-lang/target/release/kenga.exe
kernel/kenga/kmod.c: kernel/kenga/kmod.kenga
	$(KENGA) emit-c $< --freestanding -o $@

# === Цели ===
KERNEL = build/kernel.elf
ISO    = build/kengaos.iso

.PHONY: all clean iso run-iso fetch-limine

all: $(KERNEL)

$(OBJDIR):
	@mkdir -p $(OBJDIR)
	@for d in $(sort $(dir $(C_SOURCES) $(ASM_SOURCES))); do \
	    mkdir -p $(OBJDIR)/$$d; \
	done

$(OBJDIR)/%.o: %.c | $(OBJDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o: %.S | $(OBJDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL): $(OBJECTS) kernel/arch/x86_64/linker.ld
	@mkdir -p build
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)
	@echo "=== Ядро собрано: $@ ==="

iso: $(ISO)

USER_PROG = build/hello.elf
INITRD = build/initrd.tar

# User program
$(USER_PROG): user/hello.c user/userlib.h | $(OBJDIR)
	@mkdir -p build
	$(CC) -nostdlib -nostartfiles -ffreestanding -fno-pie -fno-stack-protector \
	    -fno-pic -m64 -march=x86-64 -O2 -Wall -Iuser \
	    -c user/hello.c -o $(OBJDIR)/hello.o
	$(LD) -m elf_x86_64 --image-base=0x100000 -Ttext=0x100000 -e_start -o $@ $(OBJDIR)/hello.o
	@echo "=== User program: $@ ==="

$(INITRD): initrd/* $(USER_PROG)
	@echo "=== Сборка initrd ==="
	cp $(USER_PROG) initrd/hello.elf
	cd initrd && tar -cf ../$(INITRD) --format=ustar *.txt hello.elf
	rm -f initrd/hello.elf

$(ISO): $(KERNEL) $(INITRD) limine.conf scripts/fetch-limine.sh
	./scripts/fetch-limine.sh
	@mkdir -p build/iso_root/boot/limine
	@mkdir -p build/iso_root/EFI/BOOT
	cp $(KERNEL) build/iso_root/boot/kernel.elf
	cp $(INITRD) build/iso_root/boot/initrd.tar
	cp limine.conf build/iso_root/boot/limine/limine.conf
	cp limine/limine-bios.sys build/iso_root/boot/limine/
	cp limine/limine-bios-cd.bin build/iso_root/boot/limine/
	cp limine/limine-uefi-cd.bin build/iso_root/boot/limine/
	cp limine/BOOTX64.EFI build/iso_root/EFI/BOOT/
	cp limine/BOOTIA32.EFI build/iso_root/EFI/BOOT/ 2>/dev/null || true
	$(XORRISO) -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin \
	    -no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus \
	    -apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin \
	    -efi-boot-part --efi-boot-image --protective-msdos-label \
	    build/iso_root -o $(ISO)
	./limine/bin/limine bios-install $(ISO)
	@echo "=== ISO собран: $@ ==="

fetch-limine:
	./scripts/fetch-limine.sh

run-iso: $(ISO)
	qemu-system-x86_64 -cdrom $(ISO) -m 256M -serial stdio \
	    -display gtk -cpu host -enable-kvm 2>/dev/null || \
	qemu-system-x86_64 -cdrom $(ISO) -m 256M -serial stdio -display sdl

run-iso-nographic: $(ISO)
	qemu-system-x86_64 -cdrom $(ISO) -m 256M -serial stdio -display none

# Запуск ядра напрямую в QEMU (без Limine/ISO, без initrd).
# Полезно для быстрого smoke-теста ядра.
run-direct: $(KERNEL)
	qemu-system-x86_64 -kernel $(KERNEL) -m 256M -serial stdio -display none

clean:
	rm -rf $(OBJDIR) $(KERNEL) $(ISO) $(INITRD) $(USER_PROG) build/iso_root
