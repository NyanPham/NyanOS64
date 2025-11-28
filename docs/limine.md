
# Limine

## Protocol
Limine is a bootloader that eases the work of OSDevers a lot, which makes it a perfect choice for hobbists of OSDev.

Limine protocol sets up a higher-half direct map (hhdm), and a versy simple GDT is provided. Of course, we'll need to update the GDT in our kernel after it's booted successfully.

```c
struct limine_example_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_example_response *response;
    // other members...
};
```

Each limine's request has 3 fields:
- id: each request is unique
- revision: the revision of the request that kernel provides
- response: filled by limine at load time. 

To compile our kernel with limine as a bootloader, we need to tell linker "Hey, these areas in this limine code are needed". 

```lds.requests :
{
    KEEP(*(.requests_start_marker))
    KEEP(*(.requests))
    KEEP(*(.requests_end_marker))
} :requests
```

In our kernel, say we want to access the framebuffer to paint to the screen, we request for the framebuffer from limine:
```c
__attribute__((used, section(".requests")))
static volatile struct limine_framebuffer_request {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

if ( framebuffer_request->response != NULL) {
    // Do something with the response
}
```

## Higher Half Kernel
The idea is that the whole kernel code is placed in the higher half of the address space. 
The lower half is dedicated to userspace. 

Memory Management Unit (MMU), which is built in most modern CPUs, is responsible for translating what addresses CPUs see into the real, physical addresses. 

### HHDM (Higher-Half Direct Map)

The HHDM is the mechanism Limine uses to make the Higher Half Kernel concept work easily. It's a direct mapping of all physical memory into the higher half of the virtual address space.

For example, to access physical address `0x1000`, the kernel accesses the virtual address `0x1000 + hhdm_offset`. This is useful before the kernel's own virtual memory manager is fully initialized. The kernel gets this `hhdm_offset` by using `limine_hhdm_request`.
