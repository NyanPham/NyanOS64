# 🐱 NyanOS64

![C](https://img.shields.io/badge/c-%2300599C.svg?style=for-the-badge&logo=c&logoColor=white)
![Assembly](https://img.shields.io/badge/assembly-%231572B6.svg?style=for-the-badge&logo=assembly&logoColor=white)
![QEMU](https://img.shields.io/badge/QEMU-%23FF6600.svg?style=for-the-badge&logo=qemu&logoColor=white)

NyanOS64 is my hobby OS built from the very scratch (well except for the bootloader which is Limine). It's a 64-bit OS with preemptive multitasking feature, including a custom window manager, a virtual file system, and a suite of userland applications.

This project has filled my last 6 months with pressure, stress, uncertainty, yet curiosity and joy in systems programming.

## 📸 Showcase

[![NyanOS64 Demo Workspace](https://img.youtube.com/vi/O8ObfAYi2Xg/maxresdefault.jpg)](https://youtu.be/O8ObfAYi2Xg)

Please click on the image to run the demo.

## ✨ Core Features

NyanOS64 is built from scratch, with some custom-made subsystems (except for the bootloader, which is Limine):

- **Kernel & Memory:** 64-bit Long Mode with _Physical/Virtual Memory Manager_.
- **Process Management:** Loads and runs ELF binaries in Ring 3. They are tasks/processes that are controlled by a _Preemptive Scheduler_.
- **Inter-Process Communication (IPC):** Supports pipes, shared memory, and message queue.
- **File System:** A _VFS_ layer for abstract underlying storage. It supports _FAT32 (Read/Write)_ and _TAR archives (READ)_.
- **Graphics & GUI:** A _Window Manager_ with double-buffered rendering to avoid flickering, plus movable and resizable windows.
- **Userland Apps:** Includes `NyanShell` as a terminal, `Nyamo` for text editing (with soft-wrap), and a native `Snake` game.

## 🏗️ Architecture Highlight

NyanOS64 demonstrates solutions to some of the most complex aspects of system architecture. What are they?

- **Higher Half Kernel:** The OS follows a design of higher half architecture. That means the kernel is mapped to the upper space of 64-bit virtual address. This helps separate the kernel and user-space apps, thus improving security and stability.

- **GDT, IDT, and Interrupt Handling:** Shipped with native Global Descriptor Table (GDT) and Interrupt Descriptor Table (IDT), both are built from scratch. Thanks to them, the OS can safely transition into Long Mode from the Limine boot phase. Hardware interrupts (IRQs) and CPU exceptions (#PF is what I struggled the most :/) are caught and handled effectively. That's how in/out events are routed to event queue.

- **Memory Management Unit (MMU):**
  - **PMM & VMM:** Implements x86_64 4-level paging to manage physical frames, map them to virtual memory for user-space tasks to use safely. The `sbrk` is also supported.
  - **Custom Heap Allocator:** Implements `kmalloc` and `kfree` with block splitting and contiguous free-block coalescing to avoid memory fragmentation.

- **Preemptive Task Scheduler:**
  Hardware timer interrupts are utilized to help scheduler to perform context switching from a task/process to another. The function saves, restores CPU registers (FPU included), then jumps between Ring 0 (Kernel) and Ring 3 (User-space).

## 🚀 Build & Run

To build and run NyanOS64 on your machine, you will need a Linux environment with the following dependencies installed:

### Prerequisites

- `make` and `gcc`
- `nasm`
- `xorriso`, `mtools`, and `dosfstools`
- `qemu-system-x86_64`

### Build and Run

1. Clone the repository:

```bash
git clone https://github.com/NyanPham/NyanOS64.git
cd NyanOS64
```

2. Generate the FAT32 hard drive image:

```bash
./reset_disk.sh
```

3. Boot the OS in QEMU:

```bash
make run
```

4. To clean the build files:

```bash
make clean
```

## 📂 Project Structure & File System

In NyanOS64, system resources and user data are separated.

Virtual File System (VFS):

- /bin/ and /assets/: System files and native `exe` files loaded into RAM at boot (Tar, rootfs). Hightlights are terminal, snake game, and view_bmp.
- /data/: Mounted from `hdd.img` to support FAT32 for persistent data manipulation. Except `test.txt` and `test2.txt` as they are overwritten by the Kernel after boot.

Source code layout:

- `src/` - The source code of the core Kernel, such as `mem`, `sched`, and `arch`.
- `src/libc/` - Custom implementation for the C libary.
- `progs/` - User-space apps (`nyamo.c`, `terminal.c`, `shell.c`, etc.).
- `GNUmakefile` - The system to build, clean the compiled files.

## 🙏 Acknowledgments

This project journey would not have been possible without free, community-based resources online:

- [Operating Systems: From 0 to 1](https://github.com/tuhdo/os01) - My proud ignitor for this project.
- [OSDev Wiki](https://wiki.osdev.org/) - The bible, the source of truth of OSDev :3.
- [Limine Bootloader](https://github.com/limine-bootloader/limine) - For handling the complex 64-bit boot process.
- [DreamportDev - Osdev Notes](https://github.com/dreamportdev/Osdev-Notes) - Friendly notes to explain complex concepts in OSDev, structured as book, and is much less academic than OSDev wiki.
