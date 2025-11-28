# Global Descriptor Table

Important registers: `CR0`, `EFER`

Descriptors are 8-byte wide. A selector is used to refer to a descriptor.

Some examples:
- null descriptor: selector 0x0
- code segment descriptor: selector 0x08
- data segment descriptor: selector 0x10
- etc...

TSS descriptor comes as an exception, which is 16-byte wide.

As NyanOS is for 64-bit, we'll focus on long-mode.

## GDT in Long Mode
In Long mode, GDT is mainly used to specify the ring level. We use only 2 rings: ring 0 (kernel) and ring 3 (user).

All segments are flatten, which means they all have the same base of 0, and an infinite limit (size). 

## Terms
- Descriptor: an entry in the GDT
- Selector: byte offset into the GDT. The lower 3 bits are extra fields.
- Segment: the region of memory described by the base and limit of a descriptor.

Here are the segment registers: 
- CS: code selector, where an instruction can be fetched
- DS: data selector, where memory can be accessed
- SS: stack selector, push/pop operations happen
- ES: extra selector, intended for string operations
- FS: no purpose
- GS: no purpose

A selector has the following info:
- index (selector[15:3]): GDT selector
- TI (selector[2]): 0 -> GDT, 1 -> LDT (Local Descriptor Table)
- RPL (selector[1:0]): Requested Privilege Level. 

## Segmentation
Segmentation is to enhance the security of the OS. 
We place code in one region of memory, define a segment with the base and limit (size) of that region to tell the CPU that we can fetch and execute code within this window. If we try to fetch instructions outside of this region, the CPU will raise #GP fault.

## Using GDT
In NyanOS, I applied a popular, simple setup: 
- sel 0x00: null descriptor
- sel 0x08: kernel code
- sel 0x10: kernel data
- sel 0x18: user code
- sel 0x20: user data
- sel 0x28: TSS (Task State Segment)

### Task State Segment (TSS)

Even in Long Mode, where hardware task switching via the TSS is no longer used, the TSS is still crucial. Its primary role is to tell the CPU which stack to use when an interrupt or syscall causes a privilege level change from user mode (ring 3) to kernel mode (ring 0).

The `RSP0` field in the TSS holds the address of the top of the kernel's stack. This is a critical security feature. In NyanOS, we set this up in `kmain` via `tss_set_stack()`.
