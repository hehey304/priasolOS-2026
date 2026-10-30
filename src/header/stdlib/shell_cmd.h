#ifndef SHELL_CMD_H
#define SHELL_CMD_H

#define MAX_IO_BUFFER 4096 

struct ShellIO {
    char input_buffer[MAX_IO_BUFFER]; 
    char output_buffer[MAX_IO_BUFFER]; 
    int  output_len;
};

typedef void (*CommandFunc)(int argc, char **argv, struct ShellIO *io);

void cmd_ls(int argc, char **argv, struct ShellIO *io);
void cmd_cd(int argc, char **argv, struct ShellIO *io);
void cmd_mkdir(int argc, char **argv, struct ShellIO *io);
void cmd_cat(int argc, char **argv, struct ShellIO *io);
void cmd_grep(int argc, char **argv, struct ShellIO *io);

#endif