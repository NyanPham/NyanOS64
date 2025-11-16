# Non-Maskable Interrupte (NMI)

## Definition

The Non-Maskable Interrupt (NMI) is a hardware-driven interrupt much like the PIC interrupts, but the NMI goes either directly to the CPU, or via another controller (e.g., the ISP)---in which case it can be masked.

## Reasons

There are 2 reasons for an NMI:

- A hardware failure:
  We have no way to tell which hardware fails, so the kernel can inform users that it needs to shutdown/restart.
- Watchdog timer:
  We must set this up in our OS first, even when the chipset doens't support watchdog timer.

## Usage:

NMI is enabled (set high) by the memory module when a memory parity error occurs.

**Don't** disable the NMI and PIC for a long time, watchdog timers need them so bad.

```C
void NMI_enable()
{
    outb(0x70, inb(0x70) & 0x7F);
    inb(0x71);
}

void NMI_disable()
{
    outb(0x70, inb(0x70) | 0x80);
    inb(0x71);
}
```

Check the system control port A and B at I/O addresses 0x92 and 0x61 to get more info:

**System Control Port A (0x92) layout:**
| BIT |Description |
| --- | --- |
| 0 | Alternate hot reset |
| 1 | Alternate gate A20 |
| 2 | Reserved |
| 3 | Security Lock |
| 4\* | Watchdog timer status |
| 5 | Reserved |
| 6 | HDD 2 drive activity |
| 7 | HDD 1 drive activity |

**System Control Port B (0x61)**:

| Bit | Description             |
| --- | ----------------------- |
| 0   | Timer 2 tied to speaker |
| 1   | Speaker data enable     |
| 2   | Parity check enable     |
| 3   | Channel check enable    |
| 4   | Refresh request         |
| 5   | Timer 2 output          |
| 6\* | Channel check           |
| 7\* | Parity check            |
