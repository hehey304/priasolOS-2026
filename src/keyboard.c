#include "header/driver/keyboard.h"
#include "header/cpu/portio.h"
#include "header/stdlib/string.h"

const char keyboard_scancode_1_to_ascii_map[256] = {
      0, 0x1B, '1', '2', '3', '4', '5', '6',  '7', '8', '9',  '0',  '-', '=', '\b', '\t',
    'q',  'w', 'e', 'r', 't', 'y', 'u', 'i',  'o', 'p', '[',  ']', '\n',   0,  'a',  's',
    'd',  'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',   0, '\\',  'z', 'x',  'c',  'v',
    'b',  'n', 'm', ',', '.', '/',   0, '*',    0, ' ',   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0, '-',    0,    0,   0,  '+',    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,

      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
};

const char keyboard_scancode_1_to_ascii_shift_map[128] = {
      0, 0x1B, '!', '@', '#', '$', '%', '^',  '&', '*', '(',  ')',  '_', '+', '\b', '\t',
    'Q',  'W', 'E', 'R', 'T', 'Y', 'U', 'I',  'O', 'P', '{',  '}', '\n',   0,  'A',  'S',
    'D',  'F', 'G', 'H', 'J', 'K', 'L', ':',  '"', '~',   0,  '|',  'Z', 'X',  'C',  'V',
    'B',  'N', 'M', '<', '>', '?',   0, '*',    0, ' ',
};

static bool shift_pressed = false;

static struct KeyboardDriverState keyboard_state = {
    .read_extended_mode = false,
    .keyboard_input_on  = false,
    .keyboard_buffer    = 0,
};

void keyboard_state_activate(void) {
    keyboard_state.keyboard_input_on = true;
}

void keyboard_state_deactivate(void) {
    keyboard_state.keyboard_input_on = false;
}

void get_keyboard_buffer(char *buf) {
    *buf = keyboard_state.keyboard_buffer;
    keyboard_state.keyboard_buffer = 0;
}

void keyboard_isr(void) {
    uint8_t scancode = in(KEYBOARD_DATA_PORT);

    if (keyboard_state.keyboard_input_on) {
        if (scancode == EXTENDED_SCANCODE_BYTE) {
            keyboard_state.read_extended_mode = true;
        } else if (keyboard_state.read_extended_mode) {
            keyboard_state.read_extended_mode = false;
        } else if (scancode == 0x2A || scancode == 0x36) {      // Shift pressed
            shift_pressed = true;
        } else if (scancode == 0xAA || scancode == 0xB6) {      // Shift released
            shift_pressed = false;
        } else if (!(scancode & 0x80)) {
            char ascii = shift_pressed
                ? keyboard_scancode_1_to_ascii_shift_map[scancode]
                : keyboard_scancode_1_to_ascii_map[scancode];
            if (ascii) keyboard_state.keyboard_buffer = ascii;
        }
    }

    pic_ack(IRQ_KEYBOARD);
}