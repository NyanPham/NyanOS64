#ifndef APIC_H
#define APIC_H

void apic_init(void);
void lapic_send_eoi(void);

#endif