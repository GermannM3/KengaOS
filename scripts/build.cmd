@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0\.."

set KENGA_ROOT=%cd%\kenga-lang
set BUILD_DIR=%cd%\build
set KERNEL_DIR=%cd%\kernel

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

echo [0/7] Ensuring kenga compiler is built ...
if exist "%KENGA_ROOT%\target\release\kenga.exe" goto kenga_ok
if exist "%KENGA_ROOT%\Cargo.toml" (
    pushd "%KENGA_ROOT%"
    call cargo build --release
    if errorlevel 1 goto :fail
    popd
) else (
    echo kenga.exe not built and no kenga-lang\Cargo.toml found
    echo Try: cd kenga-lang ^&^& cargo build --release
    echo Skipping C generation, using stub if present.
    goto kenga_skip
)
:kenga_ok
echo [1/7] Compiling kmain.kenga -> kmain.c via kenga emit-c --freestanding ...
"%KENGA_ROOT%\target\release\kenga.exe" emit-c --freestanding "%KERNEL_DIR%\kmain.kenga" -o "%BUILD_DIR%\kmain.c"
if errorlevel 1 goto :fail
:kenga_skip

echo [2/7] Assembling start.S ...
where x86_64-elf-gcc >nul 2>&1 && (
    x86_64-elf-gcc -c -ffreestanding -mcmodel=large -mno-red-zone -m64 -O2 -Wall -Wextra "%KERNEL_DIR%\start.S" -o "%BUILD_DIR%\start.o"
    if errorlevel 1 goto :fail
    goto :cc_ok
)
where gcc >nul 2>&1 && (
    gcc -c -ffreestanding -mcmodel=large -mno-red-zone -m64 -O2 -Wall -Wextra "%KERNEL_DIR%\start.S" -o "%BUILD_DIR%\start.o"
    if errorlevel 1 goto :fail
    goto :cc_ok
)
echo No x86_64-elf-gcc or gcc on PATH
:cc_ok

echo [3/7] Compiling kmain.c (freestanding, no libc) ...
if exist "%BUILD_DIR%\kmain.c" (
    where x86_64-elf-gcc >nul 2>&1 && (
        x86_64-elf-gcc -c -ffreestanding -nostdinc -mcmodel=large -mno-red-zone -m64 -O2 -Wall -Wextra -I"%KERNEL_DIR%" -include kf_rt.h "%BUILD_DIR%\kmain.c" -o "%BUILD_DIR%\kmain.o"
        if errorlevel 1 goto :fail
        goto :km_ok
    )
    where gcc >nul 2>&1 && (
        gcc -c -ffreestanding -nostdinc -mcmodel=large -mno-red-zone -m64 -O2 -Wall -Wextra -I"%KERNEL_DIR%" -include kf_rt.h "%BUILD_DIR%\kmain.c" -o "%BUILD_DIR%\kmain.o"
        if errorlevel 1 goto :fail
        goto :km_ok
    )
    echo No x86_64-elf-gcc or gcc
    :km_ok
) else (
    echo kmain.c not generated, skipping compile step
)

echo [4/7] Compiling kf_alloc.c (kernel FFI allocator) ...
where x86_64-elf-gcc >nul 2>&1 && (
    x86_64-elf-gcc -c -ffreestanding -mcmodel=large -mno-red-zone -m64 -O2 -Wall -Wextra "%KERNEL_DIR%\kf_alloc.c" -o "%BUILD_DIR%\kf_alloc.o"
    if errorlevel 1 goto :fail
    goto :kf_ok
)
where gcc >nul 2>&1 && (
    gcc -c -ffreestanding -mcmodel=large -mno-red-zone -m64 -O2 -Wall -Wextra "%KERNEL_DIR%\kf_alloc.c" -o "%BUILD_DIR%\kf_alloc.o"
    if errorlevel 1 goto :fail
    goto :kf_ok
)
echo No C compiler for kf_alloc.c
:kf_ok

echo [4b/7] Compiling kf_fb.c (framebuffer driver) ...
where x86_64-elf-gcc >nul 2>&1 && (
    x86_64-elf-gcc -c -ffreestanding -mcmodel=large -mno-red-zone -m64 -O2 -Wall -Wextra "%KERNEL_DIR%\kf_fb.c" -o "%BUILD_DIR%\kf_fb.o"
    if errorlevel 1 goto :fail
    goto :kffb_ok
)
where gcc >nul 2>&1 && (
    gcc -c -ffreestanding -mcmodel=large -mno-red-zone -m64 -O2 -Wall -Wextra "%KERNEL_DIR%\kf_fb.c" -o "%BUILD_DIR%\kf_fb.o"
    if errorlevel 1 goto :fail
    goto :kffb_ok
)
echo No C compiler for kf_fb.c
:kffb_ok

echo [4c/7] Compiling intr.c + isr.S (interrupts) ...
where x86_64-elf-gcc >nul 2>&1 && (
    x86_64-elf-gcc -c -ffreestanding -mcmodel=large -mno-red-zone -m64 -O2 -Wall -Wextra "%KERNEL_DIR%\intr.c" -o "%BUILD_DIR%\intr.o"
    x86_64-elf-gcc -c -ffreestanding -mcmodel=large -mno-red-zone -m64 -O2 -Wall -Wextra "%KERNEL_DIR%\isr.S" -o "%BUILD_DIR%\isr.o"
    if errorlevel 1 goto :fail
    goto :intr_ok
)
where gcc >nul 2>&1 && (
    gcc -c -ffreestanding -mcmodel=large -mno-red-zone -m64 -O2 -Wall -Wextra "%KERNEL_DIR%\intr.c" -o "%BUILD_DIR%\intr.o"
    gcc -c -ffreestanding -mcmodel=large -mno-red-zone -m64 -O2 -Wall -Wextra "%KERNEL_DIR%\isr.S" -o "%BUILD_DIR%\isr.o"
    if errorlevel 1 goto :fail
    goto :intr_ok
)
echo No C compiler for intr.c/isr.S
:intr_ok

echo [5/7] Linking kengaos.elf ...
set OBJS=%BUILD_DIR%\start.o %BUILD_DIR%\kmain.o
if exist "%BUILD_DIR%\kf_alloc.o" set OBJS=%OBJS% %BUILD_DIR%\kf_alloc.o
if exist "%BUILD_DIR%\kf_fb.o" set OBJS=%OBJS% %BUILD_DIR%\kf_fb.o
if exist "%BUILD_DIR%\intr.o" set OBJS=%OBJS% %BUILD_DIR%\intr.o
if exist "%BUILD_DIR%\isr.o" set OBJS=%OBJS% %BUILD_DIR%\isr.o
if exist "%BUILD_DIR%\start.o" (
    if exist "%BUILD_DIR%\kmain.o" (
        where x86_64-elf-ld >nul 2>&1 && (
            x86_64-elf-ld -n -nostdlib -T "%KERNEL_DIR%\linker.ld" %OBJS% -o "%BUILD_DIR%\kengaos.elf"
            if errorlevel 1 goto :fail
            goto :link_ok
        )
        where ld >nul 2>&1 && (
            ld -n -nostdlib -T "%KERNEL_DIR%\linker.ld" %OBJS% -o "%BUILD_DIR%\kengaos.elf"
            if errorlevel 1 goto :fail
            goto :link_ok
        )
        echo No linker found
        :link_ok
    )
)

echo [6/7] Preparing Limine ISO image ...
if not exist "%BUILD_DIR%\iso_root" mkdir "%BUILD_DIR%\iso_root"
if not exist "%BUILD_DIR%\iso_root\boot" mkdir "%BUILD_DIR%\iso_root\boot"
if exist "%BUILD_DIR%\kengaos.elf" copy /y "%BUILD_DIR%\kengaos.elf" "%BUILD_DIR%\iso_root\boot\kengaos.elf" >nul
copy /y "%KERNEL_DIR%\limine.cfg" "%BUILD_DIR%\iso_root\boot\limine.conf" >nul
if exist "%cd%\limine" (
    if exist "%cd%\limine\limine-bios.sys" copy /y "%cd%\limine\limine-bios.sys" "%BUILD_DIR%\iso_root\limine-bios.sys" >nul
    if exist "%cd%\limine\limine-bios-cd.bin" copy /y "%cd%\limine\limine-bios-cd.bin" "%BUILD_DIR%\iso_root\boot\limine-bios-cd.bin" >nul
    if exist "%cd%\limine\limine-uefi-cd.bin" copy /y "%cd%\limine\limine-uefi-cd.bin" "%BUILD_DIR%\iso_root\boot\limine-uefi-cd.bin" >nul
)

where xorriso >nul 2>&1 && (
    xorriso -as mkisofs -b boot/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table ^
        --efi-boot boot/limine-uefi-cd.bin -efi-boot-part --efi-boot-image --protective-msdos-label ^
        "%BUILD_DIR%\iso_root" -o "%BUILD_DIR%\kengaos.iso" 2>nul
    if exist "%BUILD_DIR%\kengaos.iso" echo wrote "%BUILD_DIR%\kengaos.iso"
)

echo [7/7] Smoke test: run QEMU and capture UART for 3s ...
where qemu-system-x86_64 >nul 2>&1 && (
    if exist "%BUILD_DIR%\kengaos.iso" (
        qemu-system-x86_64 -M q35 -cdrom "%BUILD_DIR%\kengaos.iso" -serial file:"%BUILD_DIR%\uart.log" ^
            -display none -no-reboot -no-shutdown -m 64 ^
            -device isa-debug-exit,iobase=0xf4,iosize=0x04
        echo UART log:
        if exist "%BUILD_DIR%\uart.log" type "%BUILD_DIR%\uart.log"
        findstr /C:"Hello from Kenga kernel" "%BUILD_DIR%\uart.log" >nul 2>&1 && findstr /C:"KengaOS" "%BUILD_DIR%\uart.log" >nul 2>&1
        if errorlevel 1 echo WARNING: boot markers missing from uart.log
    ) else if exist "%BUILD_DIR%\kengaos.elf" (
        echo warning: no ISO, trying direct -kernel (may not boot with Limine headers)
    )
)

echo.
echo Build complete.
echo Files:
dir /b "%BUILD_DIR%"
exit /b 0

:fail
echo Build FAILED.
exit /b 1