#ifndef SHELL_CMD_H
#define SHELL_CMD_H

#include <stdint.h>

#define MAX_IO_BUFFER 4096

struct ShellIO {
    char input_buffer[MAX_IO_BUFFER];
    char output_buffer[MAX_IO_BUFFER];
    int  output_len;
};

void cmd_cd(int argc, char **argv, struct ShellIO *io, uint32_t *cwd);
void cmd_ls(int argc, char **argv, struct ShellIO *io, uint32_t cwd);
void cmd_mkdir(int argc, char **argv, struct ShellIO *io, uint32_t cwd);
void cmd_cat(int argc, char **argv, struct ShellIO *io, uint32_t cwd);
void cmd_grep(int argc, char **argv, struct ShellIO *io);

#endif