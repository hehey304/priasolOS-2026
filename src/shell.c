#include <stdint.h>
#include <stdbool.h>
#include "header/syscall_nums.h"
#include "header/shell_cmd.h"
#include "header/stdlib/string.h" 

// Wrapper Syscall
void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile("mov %0, %%ebx" : /* <Empty> */ : "r"(ebx));
    __asm__ volatile("mov %0, %%ecx" : /* <Empty> */ : "r"(ecx));
    __asm__ volatile("mov %0, %%edx" : /* <Empty> */ : "r"(edx));
    __asm__ volatile("mov %0, %%eax" : /* <Empty> */ : "r"(eax));
    __asm__ volatile("int $0x30");
}

void print_str(char *str) {
    syscall(SYSCALL_PUTS, (uint32_t) str, 0, 0xF); 
}

uint32_t current_cwd_inode = 1; 
char cwd_path[256] = "/"; 

int main(void) {
    char input_buf[256];
    syscall(SYSCALL_KEYBOARD, 0, 0, 0); 

    while (true) {
        print_str("G10-OS@");
        print_str(cwd_path);
        print_str(" $ ");

        int idx = 0;
        char c;
        while (true) {
            syscall(SYSCALL_GETCHAR, (uint32_t) &c, 0, 0); 
            if (c == '\n') {
                input_buf[idx] = '\0';
                print_str("\n");
                break;
            } else if (c == '\b') { 
                if (idx > 0) {
                    idx--;
                    print_str("\b \b"); 
                }
            } else if (c != 0 && idx < 255) { 
                input_buf[idx++] = c;
                char temp[2] = {c, '\0'};
                print_str(temp); 
            }
        }

        if (idx == 0) continue;

        struct ShellIO io = {0};
        io.output_len = 0;

        char *commands[10]; 
        int cmd_count = 0;
        
        commands[cmd_count++] = input_buf;
        for (int i = 0; input_buf[i] != '\0'; i++) {
            if (input_buf[i] == '|') {
                input_buf[i] = '\0'; 
                commands[cmd_count++] = &input_buf[i + 1]; 
            }
        }

        for (int c_idx = 0; c_idx < cmd_count; c_idx++) {
            char *cmd_str = commands[c_idx];
            char *argv[16];
            int argc = 0;
            bool in_word = false;

            for (int i = 0; cmd_str[i] != '\0' && argc < 16; i++) {
                if (cmd_str[i] != ' ' && !in_word) {
                    argv[argc++] = &cmd_str[i];
                    in_word = true;
                } else if (cmd_str[i] == ' ' && in_word) {
                    cmd_str[i] = '\0';
                    in_word = false;
                }
            }

            if (argc == 0) continue;

            if (strcmp(argv[0], "ls") == 0) {
                cmd_ls(argc, argv, &io, current_cwd_inode);
            } else if (strcmp(argv[0], "cd") == 0) {
                uint32_t old_cwd = current_cwd_inode;
                cmd_cd(argc, argv, &io, &current_cwd_inode);
                
                if (old_cwd != current_cwd_inode) {
                    if (strcmp(argv[1], "..") == 0) {
                        int len = strlen(cwd_path);
                        if (len > 1) { // Bukan root
                            for (int i = len - 1; i >= 0; i--) {
                                if (cwd_path[i] == '/') {
                                    if (i == 0) cwd_path[1] = '\0'; 
                                    else cwd_path[i] = '\0';
                                    break;
                                }
                            }
                        }
                    } else if (strcmp(argv[1], ".") != 0) {
                        if (strcmp(cwd_path, "/") != 0) strcat(cwd_path, "/");
                        strcat(cwd_path, argv[1]);
                    }
                }
            } else if (strcmp(argv[0], "mkdir") == 0) {
                cmd_mkdir(argc, argv, &io, current_cwd_inode);
            } else if (strcmp(argv[0], "cat") == 0) {
                cmd_cat(argc, argv, &io, current_cwd_inode);
            } else if (strcmp(argv[0], "grep") == 0) {
                cmd_grep(argc, argv, &io);
            } else if (strcmp(argv[0], "find") == 0) {
                cmd_find(argc, argv, &io);
            } else if (strcmp(argv[0], "cp") == 0) {
                cmd_cp(argc, argv, &io, current_cwd_inode);
            } else if (strcmp(argv[0], "rm") == 0) {
                cmd_rm(argc, argv, &io, current_cwd_inode);
            } else if (strcmp(argv[0], "mv") == 0) {
                cmd_mv(argc, argv, &io, current_cwd_inode);
            } else {
                char *err = "Command not found\n";
                memcpy(io.output_buffer, err, strlen(err));
                io.output_len = strlen(err);
            }

            if (c_idx < cmd_count - 1) {
                memcpy(io.input_buffer, io.output_buffer, io.output_len);
                io.input_buffer[io.output_len] = '\0'; 
                io.output_len = 0; 
            }
        }

        if (io.output_len > 0) {
            io.output_buffer[io.output_len] = '\0';
            print_str(io.output_buffer);
        }
    }
    return 0;
}
