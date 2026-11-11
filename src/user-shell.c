#include <stdint.h>
#include <stdbool.h>

#define INPUT_MAX 128

void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile("mov %0, %%ebx" : : "r"(ebx));
    __asm__ volatile("mov %0, %%ecx" : : "r"(ecx));
    __asm__ volatile("mov %0, %%edx" : : "r"(edx));
    __asm__ volatile("mov %0, %%eax" : : "r"(eax));
    // gcc often uses eax as an intermediate, so it must be the last mov
    __asm__ volatile("int $0x30");
}

// helper: print one character (syscall 5, ebx = the character value)
static void putchar(char c) {
    syscall(5, (uint32_t)(uint8_t) c, 0xF, 0);
}

// placeholder until the real command parser exists
static void run_command(const char *cmd) {
    (void) cmd;   // TODO: parse and execute
}

int main(void) {
    syscall(6, (uint32_t) "shell ready\n", 12, 0xF);   // puts
    syscall(7, 0, 0, 0);                               // activate keyboard

    char line[INPUT_MAX];
    uint32_t len = 0;

    while (true) {
        char c = 0;
        syscall(4, (uint32_t) &c, 0, 0);               // get keyboard buffer
        if (!c) continue;

        if (c == '\n') {
            putchar('\n');
            line[len] = '\0';
            run_command(line);                   // later: parse and run the command
            len = 0;
            /* print_prompt(); */                     // later: print the current directory
        } else if (c == '\b') {
            if (len > 0) {                             // only erase what the user typed
                len--;
                putchar('\b');
            }
        } else if (len < INPUT_MAX - 1) {
            line[len++] = c;
            putchar(c);
        }
    }

    return 0;
}