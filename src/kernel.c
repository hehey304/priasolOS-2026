#include <stdint.h>
#include <stdbool.h>
#include "header/cpu/gdt.h"
#include "header/cpu/idt.h"
#include "header/cpu/interrupt.h"
#include "header/driver/keyboard.h"
#include "header/text/framebuffer.h"
#include "header/kernel-entrypoint.h"
#include "header/driver/disk.h"
#include "header/filesystem/ext2.h"

void kernel_setup(void) {
    // 1. Basic Kernel Initializations
    load_gdt(&_gdt_gdtr);
    pic_remap();
    initialize_idt();
    activate_keyboard_interrupt();
    framebuffer_clear();
    framebuffer_set_cursor(0, 0);

    // 2. Filesystem & TSS Initializations
    initialize_filesystem_ext2();
    gdt_install_tss();
    set_tss_register();

    // 3. Allocate Virtual Memory for User Program at Address 0x0
    paging_allocate_user_page_frame(&_paging_kernel_page_directory, (uint8_t*) 0);

    // 4. Load "shell" Binary from Root Directory (Inode 1) into Virtual Address 0x0
    struct EXT2DriverRequest request = {
        .buf = (uint8_t*) 0,
        .name = "shell",
        .parent_inode = 1,  
        .buffer_size = 0x100000,
        .name_len = 5,
    };
    read(request);

    // 5. Prepare TSS Kernel Stack and Jump to Ring 3
    set_tss_kernel_current_stack();
    kernel_execute_user_program((uint8_t*) 0);

    while (true);
}