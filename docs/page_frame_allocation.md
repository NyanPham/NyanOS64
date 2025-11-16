# Page Frame Allocation

**Source:** [Page Frame Allocation - ODataDev Wiki](https://wiki.osdev.org/Page_Frame_Allocation)

This document summarizes the different algorithms for page frame allocation, as described on the OSDev Wiki.

---

## 1. Physical Memory Allocators

These algorithms provide a new, usable page frame from physical memory.

### Bitmap
* **How it works:** A large array of bits (N/8 bytes for N pages) is used. Each bit represents one page (free or used).
* **Pros:** Setting the state of a page is very fast (O(1)).
* **Cons:** Allocating a page can be slow (O(N)) as it requires searching for a free bit.
* **Optimizations:**
    * Check 32 bits at once with a `uint32_t` comparison.
    * Remember the last allocated bit's position to speed up the next search.

### Stack/List of pages
* **How it works:** The address of every available physical frame is stored in a stack or a linked list.
* **Pros:** Allocating and freeing a page is very fast (O(1)).
* **Cons:** Checking the state of a specific page is difficult or impractical without additional metadata.

### Sized Portion Scheme
* **How it works:** Splits memory areas into chunks of various pre-defined sizes (e.g., one 8kb and two 4kb chunks from a 16kb area).
* **Pros:** Allows for a closer fit to the requested size, which reduces memory waste.
* **Note:** The original page includes a diagram for this (`Sized Portion Scheme.png`).

### Buddy Allocation System
* **How it works:** The allocator used by the Linux kernel. It manages memory in blocks of power-of-2 sizes (e.g., 4K, 8K, 16K...). It uses bitmaps to track the availability of these different-sized blocks.
* **Pros:** Fast at locating and merging collections of pages (buddies).
* **Example (from OSDev Wiki):**
    This shows a 4-buddy system with free (.) and used (#) pages.

    **Diagram 1: Before Allocation**
```
                 0   4   8   12  16  20  24  28  32  36
                 ###.#....#........#...###...########.... real memory pattern

   buddy[0]--->  ###.#.xx.#xxxxxxxx#.xx###.xx########xxxx 5  free 4K , 5-byte bitmap
   buddy[1]--->  # # # . # . x x . # . # # . # # # # x x  5  free 8K , 20-bits map
   buddy[2]--->  #   #   #   .   #   #   #   #   #   .    2  free 16K, 10-bits map
   buddy[3]--->  #       #       #       #       #        0  free 32K, 5-bits map
```

    **Diagram 2: After Allocating 12K**
```
                 0   4   8   12  16  20  24  28  32  36
                 ###.#....#..###...#...###...########.... real memory pattern

   buddy[0]--->  ###.#.xx.#xx###.xx#.xx###.xx########xxxx 6  free 4K , 5-byte bitmap
   buddy[1]--->  # # # . # . # # . # . # # . # # # # x x  5  free 8K , 20-bits map
   buddy[2]--->  #   #   #   #   #   #   #   #   #   .    1  free 16K, 10-bits map
   buddy[3]--->  #       #       #       #       #        0  free 32K, 5-bits map
```

* **Explanation of the 12K Allocation (Diagram 1 to 2):**
    This example shows what happens when the system requests **12K** of memory.

    1.  **The Request:** The system needs **12K**.
    2.  **Find Best Fit:** The buddy system handles memory in power-of-2 blocks (4K, 8K, 16K...). A 12K request is too big for an 8K block, so the allocator must find the next size up: a **16K block**.
    3.  **Find Free Block:** The allocator looks at `buddy[2]` (the 16K list). In **Diagram 1**, it finds a free 16K block (`.`) starting at **page #12**.
    4.  **Allocate and Split:**
        * The allocator reserves this entire 16K block (which covers pages 12, 13, 14, and 15).
        * It gives the requested **12K** (3 pages: #12, #13, #14) to the application.
        * This is why in **Diagram 2**, the "real memory pattern" at page 12 changes from `....` to `###.` (3 pages used, 1 page still free).
    5.  **Return the Remainder:**
        * The allocation leaves **4K (1 page) of memory remaining** (page #15).
        * This 4K "buddy" is returned to the free pool.
        * You can see this change in **Diagram 2**:
            * `buddy[2]` (16K list) now shows the block at page #12 as used (`#`), and the total count drops from "2 free 16K" to **"1 free 16K"**.
            * `buddy[0]` (4K list) gains the leftover page, and its count increases from "5 free 4K" to **"6 free 4K"**.

### Hybrid Schemes
* **Hybrid scheme #1:** Chains allocators. For example, use a stack for recent operations, and "commit" the bottom of the stack to a bitmap for compact storage.
* **Hybrid scheme #2:** Uses a large array of structures, where each structure represents a page. This acts as a reverse page mapping table and can store status, links, and other info.

---

## 2. Virtual Addresses Allocator

These algorithms manage large areas of virtual address space.

### Flat List
* **How it works:** Uses a linked list. Each node in the list describes a "free" region with its base address and size.
* **Cons:** Searching for a block of a specific size (O(N)) or checking if an address is free (O(N)) can be slow as the list grows and memory gets fragmented.
* **Note:** The original page includes a diagram for this (`Flat list.png`).

### Tree-based approach
* **How it works:** Uses a more efficient data structure, like a balanced binary tree (e.g., AVL Tree). Each node describes a memory region.
* **Pros:** Searching the tree is much faster (O(log N)) than a linked list.
* **Note:** Used by Linux for virtual address management. The original page includes a diagram for this (`Tree based.png`).