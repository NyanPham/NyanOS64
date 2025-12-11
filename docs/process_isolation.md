# Process Isolation

This is pretty long journey of mine. I've decided to switch from "Single Address Space" (using one PML4 for all tasks) to "Process Isolation" (each task has its own PML4)

The below is my notes:

# The Goal

A task's stack is allocated based on PID: `0x500000 + PID + 0x10000`.

When PID is big enough, we have no more space left. We need a better approach: each task has its own value for CR3.

The benefit is that though tasks have the same virt_addr, they don't share the same phys_addr. 0x400000 of Task A is different from 0x400000 of Task B. 

# VMM

VMM needs a `vmm_new_pml4()` function to request a new physical page for new PML4.

It copies the Kernel (higher half, index 256 to 511) from the current task to the new task.

# Scheduler

1. Update `struct Task`

We add the field `pml4` to struct Task.

2. Update `schedule()`

When switching from Task A to Task B, besides swapping the Stack (RSP), we need to swap the Page Table as well.

We create 2 helper functions `write_cr3` and `read_cr3`. 

3. Update `sched_exit()`

The old `kfree(task)` is not enough. A task now holds a whole new pml4 page, which is recursive in 4 levels. We need to manage the memory of page tables as well.

We create a new function `vmm_free_table` in VMM to recursively return the UserSpace's pages back to PMM.

Note: Before the cleanup, switch to the pml4 of other task, or kernel to avoid eating itself up.

# SYS_EXEC and ELF

This is nerve breaking. How to load ELF file to a new PML4 while still running on the current PML4.

1. Sandwich!

Update the `syscall.c`, case 7 (exec) with:

```
curr_pml4 = read_cr3()

write_cr3(new_task->pml4)
elf_load(fname) // this loads the ELF file to the PML4 of the new_task rather than the current task

write_cr3(curr_pml4)
```

2. Update `elf_load`

To align with the above sandwich approach, instead of always mapping phys_addr and virt_addr on the kern_pml4, we use the `read_cr3() + hhdm_offset` instead.

# Notes

Our shell is a task, and our linker sets it to run at 0x400000. If we also set USER_STACK_TOP to be 0x400000 to store the page table for the shell, we get a conflict. So we set USER_STACK_TOP to be 0x1000000 (16MB), to ensure we are safe with spacing. 

Some guards are added to `kmalloc` to make sure the returned addresses are not lower than hhdm_offset.

---

Now NyanOS has:

[x] Each process has it owns PML4.
[x] User Stacks never conflict.
[x] Loading ELF is safe.
[x] No more Memory Leak after Task Exit.