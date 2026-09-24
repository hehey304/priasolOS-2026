#include "header/cpu/gdt.h"

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
        }
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