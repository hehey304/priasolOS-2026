#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/text/framebuffer.h"
#include "header/stdlib/string.h"
#include "header/cpu/portio.h"

void framebuffer_set_cursor(uint8_t r, uint8_t c) {
    uint16_t pos = r * 80 + c;

    out(CURSOR_PORT_CMD, 0x0F);
    out(CURSOR_PORT_DATA, (uint8_t) (pos & 0xFF));

    out(CURSOR_PORT_CMD, 0x0E);
    out(CURSOR_PORT_DATA, (uint8_t) ((pos >> 8) & 0xFF));
}

void framebuffer_write(uint8_t row, uint8_t col, char c, uint8_t fg, uint8_t bg) {
    uint16_t offset = 2 * (row * 80 + col);

    FRAMEBUFFER_MEMORY_OFFSET[offset]     = c;
    FRAMEBUFFER_MEMORY_OFFSET[offset + 1] = (uint8_t) ((bg << 4) | (fg & 0x0F));
}

void framebuffer_clear(void) {
    for (uint8_t row = 0; row < 25; row++) {
        for (uint8_t col = 0; col < 80; col++) {
            framebuffer_write(row, col, 0x00, 0x07, 0x00);
        }
    }
}

static void framebuffer_scroll(void) {
    for (uint8_t row = 1; row < 25; row++) {
        for (uint8_t col = 0; col < 80; col++) {
            uint16_t src = 2 * (row * 80 + col);
            uint16_t dst = 2 * ((row - 1) * 80 + col);
            FRAMEBUFFER_MEMORY_OFFSET[dst]     = FRAMEBUFFER_MEMORY_OFFSET[src];
            FRAMEBUFFER_MEMORY_OFFSET[dst + 1] = FRAMEBUFFER_MEMORY_OFFSET[src + 1];
        }
    }
    for (uint8_t col = 0; col < 80; col++) {
        framebuffer_write(24, col, 0x00, 0x07, 0x00);
    }
}


static uint8_t cursor_row = 0;
static uint8_t cursor_col = 0;

void puts(const char *buf, uint32_t count, uint32_t color) {
    for (uint32_t i = 0; i < count; i++) {
        char c = buf[i];

        if (c == '\n') {
            cursor_col = 0;
            cursor_row++;
        } else {
            framebuffer_write(cursor_row, cursor_col, c, (uint8_t) color, 0);
            cursor_col++;
            if (cursor_col >= 80) {
                cursor_col = 0;
                cursor_row++;
            }
        }

        if (cursor_row >= 25) {
            framebuffer_scroll();
            cursor_row = 24;
        }
    }
    framebuffer_set_cursor(cursor_row, cursor_col);
}