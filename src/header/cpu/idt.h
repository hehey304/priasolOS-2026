#ifndef _IDT_H
#define _IDT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// IDT hard limit, see Intel x86 manual 3a - 6.10 Interrupt Descriptor Table[cite: 7]
#define IDT_MAX_ENTRY_COUNT    256
#define ISR_STUB_TABLE_LIMIT   64
#define INTERRUPT_GATE_R_BIT_1 0b000
#define INTERRUPT_GATE_R_BIT_2 0b110
#define INTERRUPT_GATE_R_BIT_3 0b0

// Interrupt Handler / ISR stub for reducing code duplication, this array can be iterated in initialize_idt()[cite: 7]
extern void *isr_stub_table[ISR_STUB_TABLE_LIMIT];

extern struct IDTR _idt_idtr;

/**
 * IDTGate, IDT entry that point into interrupt handler
 * Struct defined exactly in Intel x86 Vol 3a - Figure 6-2. IDT Gate Descriptors
 *
 * @param offset_low  Lower 16-bit offset
 * @param segment     Memory segment
 * @param _reserved   Reserved bit, bit length: 5
 * @param _r_bit_1    Reserved for idtgate type, bit length: 3
 * @param _r_bit_2    Reserved for idtgate type, bit length: 3
 * @param gate_32     Is this gate size 32-bit? If not then its 16-bit gate
 * @param _r_bit_3    Reserved for idtgate type, bit length: 1
 * @param privilege   Descriptor privilege level, bit length: 2
 * @param valid_bit   Segment present flag, bit length: 1
 * @param offset_high Higher 16-bit offset
 */
struct IDTGate {
    uint16_t offset_low;
    uint16_t segment;
    uint8_t _reserved : 5;
    uint8_t _r_bit_1 : 3;
    uint8_t _r_bit_2 : 3;
    uint8_t gate_32 : 1;
    uint8_t _r_bit_3 : 1;
    uint8_t privilege : 2;
    uint8_t valid_bit : 1;
    uint16_t offset_high;
} __attribute__((packed));

/**
 * Interrupt Descriptor Table, containing lists of IDTGate.
 * One IDT already defined in idt.c
 *
 * @param table Array of IDTGate with size IDT_MAX_ENTRY_COUNT
 */
struct InterruptDescriptorTable {
    struct IDTGate table[IDT_MAX_ENTRY_COUNT];
} __attribute__((packed));

/**
 * IDTR, carrying information where's the IDT located and size.
 * Global kernel variable defined at idt.c.
 *
 * @param size    Size of IDT minus 1
 * @param address Pointer to the InterruptDescriptorTable
 */
struct IDTR {
    uint16_t size;
    struct InterruptDescriptorTable *address;
} __attribute__((packed));

/**
 * Set IDTGate with proper interrupt handler values.
 * Will directly edit global IDT variable and set values properly
 * 
 * @param int_vector       Interrupt vector to handle
 * @param handler_address  Interrupt handler address
 * @param gdt_seg_selector GDT segment selector, for kernel use GDT_KERNEL_CODE_SEGMENT_SELECTOR
 * @param privilege        Descriptor privilege level
 */
void set_interrupt_gate(uint8_t int_vector, void *handler_address, uint16_t gdt_seg_selector, uint8_t privilege);

/**
 * Set IDT with proper values and load with lidt
 */
void initialize_idt(void);

#endif