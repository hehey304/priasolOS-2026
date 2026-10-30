#include "header/cpu/gdt.h"
#include <stdint.h>
#include "header/cpu/interrupt.h"

/**
 * global_descriptor_table, predefined GDT.
 * Initial SegmentDescriptor already set properly according to Intel Manual & OSDev.
 * Table entry : [{Null Descriptor}, {Kernel Code}, {Kernel Data (variable, etc)}, ...].
 */
struct GlobalDescriptorTable global_descriptor_table = {
    .table = {
        // 0: Null Descriptor
        {
            .segment_low = 0, .base_low = 0, .base_mid = 0,
            .type_bit = 0, .non_system = 0, .dpl = 0, .p = 0,
            .segment_high = 0, .avl = 0, .long_mode = 0, .db = 0,
            .granularity = 0, .base_high = 0
        },
        // 1: Kernel Code Segment
        {
            .segment_low = 0xFFFF, .base_low = 0, .base_mid = 0,
            .type_bit = 0xA,       // Code, Not Accessed, Readable, Not Conforming
            .non_system = 1,       // S = 1 (Code/Data)
            .dpl = 0,               // Ring 0
            .p = 1,                 // Present
            .segment_high = 0xF,
            .avl = 0, .long_mode = 0,
            .db = 1,                // 32-bit operand
            .granularity = 1,       // 4 KB
            .base_high = 0
        },
        // 2: Kernel Data Segment
        {
            .segment_low = 0xFFFF, .base_low = 0, .base_mid = 0,
            .type_bit = 0x2,        // Data, Not Accessed, Writable, Direction Up
            .non_system = 1,
            .dpl = 0,
            .p = 1,
            .segment_high = 0xF,
            .avl = 0, .long_mode = 0,
            .db = 1,
            .granularity = 1,
            .base_high = 0
        },
        // 3: User Code Segment
        {
            .segment_low = 0xFFFF, .base_low = 0, .base_mid = 0,
            .type_bit = 0xA,
            .non_system = 1,
            .dpl = 3,               // Ring 3
            .p = 1,
            .segment_high = 0xF,
            .avl = 0, .long_mode = 0,
            .db = 1,
            .granularity = 1,
            .base_high = 0
        },
        // 4: User Data Segment
        {
            .segment_low = 0xFFFF, .base_low = 0, .base_mid = 0,
            .type_bit = 0x2,
            .non_system = 1,
            .dpl = 3,               // Ring 3
            .p = 1,
            .segment_high = 0xF,
            .avl = 0, .long_mode = 0,
            .db = 1,
            .granularity = 1,
            .base_high = 0
        },
        // 5: TSS
        {
            .segment_low = sizeof(struct TSSEntry),
            .segment_high = (sizeof(struct TSSEntry) & (0xF << 16)) >> 16,
            .base_low = 0, .base_mid = 0, .base_high = 0,   // filled by gdt_install_tss()
            .type_bit = 0x9,        // 32-bit TSS (available)
            .non_system = 0,        // S = 0 (system descriptor)
            .dpl = 0,
            .p = 1,
            .avl = 0, .long_mode = 0,
            .db = 1,
            .granularity = 0        // byte granularity, limit is just 104 bytes
        },
        // 6: padding
        {0}
    }
};

/**
 * _gdt_gdtr, predefined system GDTR.
 * GDT pointed by this variable is already set to point global_descriptor_table above.
 * From: https://wiki.osdev.org/Global_Descriptor_Table, GDTR.size is GDT size minus 1.
 */
struct GDTR _gdt_gdtr = {
    .size    = sizeof(global_descriptor_table) - 1,
    .address = &global_descriptor_table
};

void gdt_install_tss(void) {
    uint32_t base = (uint32_t) &_interrupt_tss_entry;
    global_descriptor_table.table[5].base_high = (base & (0xFF << 24)) >> 24;
    global_descriptor_table.table[5].base_mid  = (base & (0xFF << 16)) >> 16;
    global_descriptor_table.table[5].base_low  = base & 0xFFFF;
}
