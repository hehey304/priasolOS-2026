#include <stdint.h>
#include <stdbool.h>
#include "header/cpu/gdt.h"
#include "header/cpu/idt.h"
#include "header/cpu/interrupt.h"
#include "header/driver/keyboard.h"
#include "header/text/framebuffer.h"
#include "header/kernel-entrypoint.h"
#include "header/driver/disk.h"

void kernel_setup(void) {
    // Inisialisasi GDT, PIC, dan IDT
    load_gdt(&_gdt_gdtr);
    pic_remap();
    initialize_idt();
    
    // Aktifkan interrupt keyboard pada PIC
    activate_keyboard_interrupt();
    
    // Bersihkan layar
    framebuffer_clear();
    framebuffer_set_cursor(0, 0);

    struct BlockBuffer b;
    for (int i = 0; i < 512; i++) b.buf[i] = i % 16;
    write_blocks(&b, 17, 1);
   
    int row = 0, col = 0;
    
    // Mulai menangkap input keyboard
    keyboard_state_activate();
    
    // Loop utama sistem operasi untuk mencetak input
    while (true) {
        char c;
        get_keyboard_buffer(&c);
        
        if (c) {
            framebuffer_write(row, col, c, 0xF, 0);
            if (col >= 79) { 
                ++row;
                col = 0;
            } else {
                ++col;
            }
           framebuffer_set_cursor(row, col);
        }
    }
}