# Programmable Interrupt Controller

My NyanOS doesn't use PIC, but I still keep pic code in the `drivers/legacy` folders for the previous setup. The [pic.h](../src/drivers/legacy/pic.h) and [pic.c](../src/drivers/legacy/pic.c) are pretty clear in the documents.
Here is the diagram of the whole flows, as the concepts of Interrupt Request Register (IRR), In-service Register (ISR), and Interrupt Mask Register (IMR) are pretty confusing at first.

```mermaid
graph TD
  subgraph Device
    A["1. Key is pressed"] --> B["Keyboard Controller"]
  end

  subgraph "PIC 8259"
    C["IRQ1 Pin"]
    D["Gate 1: IMR\n(Interrupt Mask Register)"]
    E["IRR\n(Interrupt Request Register)"]
    F["Priority Resolver"]
    G["Gate 2: ISR\n(In-Service Register)"]
  end

  subgraph CPU
    H["INTR Pin"]
    I["CPU Core"]
    J["IDT\n(Interrupt Descriptor Table)"]
  end

  subgraph Kernel
    K["irq1_stub (Assembly)"]
    L["irq_dispatch (C)"]
    M["keyboard_handler (C)"]
    N["pic_send_eoi(1)"]
  end

  %% --- Flow ---
  A --> B
  B -- "Sends interrupt signal" --> C
  C --> D

  D -- "Is bit 1 unmasked?\n(via pic_clear_mask)" --> E
  D -- "Is bit 1 masked?\n(via pic_set_mask)" --> X["Signal is ignored"]

  E -- "Sets bit 1 in IRR\n(Request is pending)" --> F
  F -- "Sends signal to CPU" --> H
  H --> I
  I -- "2. CPU acknowledges (INTA)" --> G

  G -- "3. Moves bit 1\nfrom IRR to ISR" --> J
  J -- "4. CPU uses vector 33\nto find handler in IDT" --> K

  K -- "Saves registers & calls C" --> L
  L -- "Calls registered handler" --> M
  M -- "Reads scancode & processes" --> N
  N -- "5. Sends EOI command" --> G
  G -- "Clears bit 1 in ISR\n(Ready for new interrupt)" --> F

  %% --- Styling ---
  style A fill:#f9f,stroke:#333,stroke-width:2px
  style X fill:#ff9999,stroke:#333,stroke-width:2px
  style D fill:#ffe4b2,stroke:#333,stroke-width:2px
  style G fill:#ffe4b2,stroke:#333,stroke-width:2px
```