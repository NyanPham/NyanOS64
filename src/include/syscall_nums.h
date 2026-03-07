#ifndef SYSCALL_NUMS_H
#define SYSCALL_NUMS_H

// === FILE SYSTEM & I/O (0 - 19) ===
#define SYS_READ 0
#define SYS_WRITE 1
#define SYS_OPEN 2
#define SYS_CLOSE 3
#define SYS_MKDIR 4
#define SYS_READDIR 5
#define SYS_UNLINK 6
#define SYS_CHDIR 7
#define SYS_GETCWD 8
#define SYS_FSTAT 9
#define SYS_PIPE 10
#define SYS_DUP2 11
#define SYS_LIST_FILES 12

// === PROCESS & TASK (20 - 29) ===
#define SYS_FORK 20
#define SYS_EXEC 21
#define SYS_EXIT 22
#define SYS_WAITPID 23
#define SYS_GETPID 24
#define SYS_SLEEP 25

// === MEMORY MANAGEMENT (30 - 39) ===
#define SYS_SBRK 30
#define SYS_MMAP 31
#define SYS_MUNMAP 32
#define SYS_FTRUNCATE 33

// === IPC (SHM & Message Queue) (40 - 49) ===
#define SYS_SHM_OPEN 40
#define SYS_MQ_OPEN 41
#define SYS_MQ_SEND 42
#define SYS_MQ_RECEIVE 43
#define SYS_MQ_UNLINK 44

// === GUI & GRAPHICS (50 - 59) ===
#define SYS_CREATE_WIN 50
#define SYS_WIN_GET_SIZE 51
#define SYS_DRAW_RECT 52
#define SYS_BLIT 53
#define SYS_GET_EVENT 54
#define SYS_SET_FG 55
#define SYS_KILL_FG 56
#define SYS_AWAIT_IO 57

// === SYSTEM & HARDWARE (60 - 69) ===
#define SYS_REBOOT 60
#define SYS_SHUTDOWN 61
#define SYS_GET_TIME 62
#define SYS_GET_KEY 63

// === DEBUG & MISC (70 - 79) ===
#define SYS_KPRINT 70
#define SYS_KPRINT_INT 71
#define SYS_CLEAR 72

#define MAX_SYSCALLS 80

#endif