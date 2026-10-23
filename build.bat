@echo off
setlocal EnableDelayedExpansion

if "%1"=="clean" goto :clean
if "%1"=="reset-disk" goto :reset_disk

:: ============================================================================
:: 1. Detect Cross-Compiler Toolchain (x86_64-elf-gcc OR clang + ld.lld)
:: ============================================================================
where x86_64-elf-gcc >nul 2>nul
if %ERRORLEVEL%==0 (
    set "CC=x86_64-elf-gcc"
    set "LD=x86_64-elf-ld"
    echo [TOOLCHAIN] Using x86_64-elf-gcc
) else (
    where clang >nul 2>nul
    if !ERRORLEVEL!==0 (
        set "CC=clang -target x86_64-unknown-none-elf"
        set "LD=ld.lld"
        echo [TOOLCHAIN] Using clang + ld.lld
    ) else (
        echo [ERROR] Neither x86_64-elf-gcc nor clang/ld.lld was found in PATH!
        echo MinGW gcc targets Windows PE/COFF, not ELF64.
        echo Install LLVM ^(winget install LLVM.LLVM^) or x86_64-elf-tools.
        exit /b 1
    )
)

:: ============================================================================
:: 2. Compiler & Linker Flags
:: ============================================================================
set "CFLAGS=-g -O2 -Wall -Wextra -std=gnu11 -ffreestanding -fno-stack-protector -fno-stack-check -fno-lto -fno-PIC -ffunction-sections -fdata-sections -m64 -march=x86-64 -mabi=sysv -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel -DDEBUG_SERIAL -Isrc -DLIMINE_API_REVISION=4"
set "NASMFLAGS=-f elf64 -g -F dwarf -Wall"
set "LDFLAGS=-m elf_x86_64 -nostdlib -static -z max-page-size=0x1000 --gc-sections -T linker.lds"

set "USER_CFLAGS=-Wall -Wextra -std=gnu11 -ffreestanding -fno-stack-protector -fno-stack-check -fno-lto -fno-PIC -m64 -march=x86-64 -mabi=sysv -mcmodel=small -Isrc/include -Isrc -DLIMINE_API_REVISION=4"
set "USER_LDFLAGS=-m elf_x86_64 -nostdlib -static -z max-page-size=0x1000"

if not exist bin mkdir bin
if not exist obj mkdir obj

:: ============================================================================
:: 3. Build Kernel Object Files
:: ============================================================================
echo [BUILD] Compiling Kernel Assembly files...
set "KERN_OBJS="
for /R src %%F in (*.asm) do (
    echo "%%F" | findstr /I /C:"crt0.asm" >nul
    if !ERRORLEVEL! NEQ 0 (
        set "OBJ_FILE=obj\kern_%%~nF.asm.o"
        nasm %NASMFLAGS% "%%F" -o "!OBJ_FILE!" || exit /b 1
        set "KERN_OBJS=!KERN_OBJS! !OBJ_FILE!"
    )
)

echo [BUILD] Compiling Kernel C files...
for /R src %%F in (*.c) do (
    echo "%%F" | findstr /I /C:"libc.c" >nul
    if !ERRORLEVEL! NEQ 0 (
        set "OBJ_FILE=obj\kern_%%~nF.c.o"
        %CC% %CFLAGS% -c "%%F" -o "!OBJ_FILE!" || exit /b 1
        set "KERN_OBJS=!KERN_OBJS! !OBJ_FILE!"
    )
)

echo [LINK] Linking bin\nyanOS...
%LD% %LDFLAGS% %KERN_OBJS% -o bin\nyanOS || exit /b 1

:: ============================================================================
:: 4. Build Userland Libc & Programs
:: ============================================================================
echo [BUILD] Compiling Userland Libc...
if not exist obj\progs mkdir obj\progs
nasm -f elf64 src\libc\crt0.asm -o obj\crt0.o || exit /b 1
%CC% %USER_CFLAGS% -c src\libc\libc.c -o obj\user_libc.o || exit /b 1
%CC% %USER_CFLAGS% -c src\libc\ansi.c -o obj\user_ansi.o || exit /b 1
set "USER_OBJS=obj\crt0.o obj\user_libc.o obj\user_ansi.o"

echo [BUILD] Building shell.elf...
%CC% %USER_CFLAGS% -c progs\shell.c -o obj\progs\shell.o || exit /b 1
%LD% %USER_LDFLAGS% -Ttext=0x400000 %USER_OBJS% obj\progs\shell.o -o shell.elf || exit /b 1

set "PROGS=terminal hello snake nyamo test_fork crash fpu_test writer reader mq_sender mq_receiver clock_digital clock_analog view_bmp test_event_queue"
for %%P in (%PROGS%) do (
    echo [BUILD] Building %%P.elf...
    %CC% %USER_CFLAGS% -c progs\%%P.c -o obj\progs\%%P.o || exit /b 1
    %LD% %USER_LDFLAGS% -Ttext=0x800000 %USER_OBJS% obj\progs\%%P.o -o %%P.elf || exit /b 1
)

:: ============================================================================
:: 5. Populate rootfs/ and Prepare Limine
:: ============================================================================
if not exist rootfs\bin mkdir rootfs\bin
if not exist rootfs\bin\tests mkdir rootfs\bin\tests
if not exist rootfs\assets mkdir rootfs\assets

for %%P in (shell terminal snake nyamo clock_digital clock_analog view_bmp) do (
    copy /Y %%P.elf rootfs\bin\ >nul
)
for %%P in (hello test_fork crash reader writer mq_receiver mq_sender fpu_test test_event_queue) do (
    copy /Y %%P.elf rootfs\bin\tests\ >nul
)

if not exist limine (
    echo [LIMINE] Cloning Limine v10.x-binary...
    git clone https://codeberg.org/Limine/Limine.git limine --branch=v10.x-binary --depth=1 || exit /b 1
)
if not exist limine\limine.exe (
    echo [LIMINE] Compiling limine.exe with host gcc...
    gcc limine\limine.c -o limine\limine.exe || exit /b 1
)

:: ============================================================================
:: 6. Build rootfs.tar, hdd.img, and image.hdd
:: ============================================================================
python make_disk.py image || exit /b 1
limine\limine.exe bios-install image.hdd || exit /b 1
echo [SUCCESS] Build complete!

if "%1"=="run" goto :run
if "%1"=="debug" goto :debug
goto :eof

:run
echo [QEMU] Booting NyanOS on Windows...
qemu-system-x86_64 -hda image.hdd -hdb hdd.img -serial stdio
goto :eof

:debug
echo [QEMU] Waiting for GDB on port 1234...
qemu-system-x86_64 -hda image.hdd -hdb hdd.img -S -s -serial stdio
goto :eof

:reset_disk
python make_disk.py reset-disk
goto :eof

:clean
echo [CLEAN] Removing build artifacts...
if exist bin rmdir /S /Q bin
if exist obj rmdir /S /Q obj
if exist image.hdd del /Q image.hdd
if exist rootfs.tar del /Q rootfs.tar
del /Q *.elf 2>nul
goto :eof