#include "header/shell_cmd.h"
#include "header/filesystem/ext2.h" 
#include "header/syscall_nums.h"
#include "header/stdlib/string.h"

extern void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx);

void cmd_cd(int argc, char **argv, struct ShellIO *io, uint32_t *cwd) {
    if (argc < 2) { 
        *cwd = 1; 
        return; 
    }
    
    char dir_buf[BLOCK_SIZE]; 
    struct EXT2DriverRequest req = {
        .parent_inode = *cwd, 
        .name = ".", 
        .name_len = 1, 
        .buffer_size = BLOCK_SIZE, 
        .buf = dir_buf
    };
    
    int32_t ret;
    syscall(SYSCALL_READ_DIR, (uint32_t) &req, (uint32_t) &ret, 0); 
    
    if (ret == 0) {
        uint32_t off = 0;
        while (off < BLOCK_SIZE) {
            uint32_t inode = *((uint32_t*)(dir_buf + off));
            if (inode == 0) break; 
            
            uint16_t rec_len = *((uint16_t*)(dir_buf + off + 4));
            uint8_t name_len = *((uint8_t*)(dir_buf + off + 6));
            uint8_t file_type = *((uint8_t*)(dir_buf + off + 7));
            char *name = dir_buf + off + 8;
            
            if (name_len == strlen(argv[1]) && memcmp(name, argv[1], name_len) == 0) {
                if (file_type == 2) { 
                    *cwd = inode;
                    return;
                } else {
                    char *err = "cd: Not a directory\n";
                    memcpy(io->output_buffer, err, strlen(err));
                    io->output_len = strlen(err);
                    return;
                }
            }
            off += rec_len;
        }
    }
    char *err = "cd: No such file or directory\n";
    memcpy(io->output_buffer, err, strlen(err));
    io->output_len = strlen(err);
}

void cmd_ls(int argc, char **argv, struct ShellIO *io, uint32_t cwd) {
    char dir_buf[BLOCK_SIZE]; 
    
    struct EXT2DriverRequest req = {
        .parent_inode = cwd,
        .name = ".", 
        .name_len = 1,
        .buffer_size = BLOCK_SIZE,
        .buf = dir_buf
    };
    
    int32_t retcode;
    syscall(SYSCALL_READ_DIR, (uint32_t) &req, (uint32_t) &retcode, 0); 
    
    if (retcode == 0) {
        uint32_t off = 0;
        int out_idx = 0;
        
        while (off < BLOCK_SIZE) {
            struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *)(dir_buf + off);
            if (entry->inode == 0) break; 
            
            memcpy(io->output_buffer + out_idx, (char*)entry + sizeof(struct EXT2DirectoryEntry), entry->name_len);
            out_idx += entry->name_len;
            io->output_buffer[out_idx++] = '\n'; 
            
            off += entry->rec_len; 
        }
        io->output_buffer[out_idx] = '\0';
        io->output_len = out_idx;
    }
}

void cmd_mkdir(int argc, char **argv, struct ShellIO *io, uint32_t cwd) {
    if (argc < 2) return;
    
    struct EXT2DriverRequest req = {
        .parent_inode = cwd,
        .name = argv[1],
        .name_len = strlen(argv[1]),
        .is_directory = true,
        .buffer_size = 0,
        .buf = 0
    };
    
    int32_t retcode;
    syscall(SYSCALL_WRITE, (uint32_t) &req, (uint32_t) &retcode, 0); 
    
    if (retcode == 1) { 
        char *msg = "mkdir: Folder sudah ada\n";
        memcpy(io->output_buffer, msg, strlen(msg));
        io->output_len = strlen(msg);
    }
}

void cmd_cat(int argc, char **argv, struct ShellIO *io, uint32_t cwd) {
    if (argc < 2) return;
    
    struct EXT2DriverRequest req = {
        .parent_inode = cwd,
        .name = argv[1],
        .name_len = strlen(argv[1]),
        .buffer_size = MAX_IO_BUFFER, 
        .buf = io->output_buffer      
    };
    
    int32_t retcode;
    syscall(SYSCALL_READ, (uint32_t) &req, (uint32_t) &retcode, 0); 
    
    if (retcode == 0) {
        io->output_len = strlen(io->output_buffer);
    } else {
        char *err = "cat: File tidak ditemukan atau bukan file\n";
        memcpy(io->output_buffer, err, strlen(err));
        io->output_len = strlen(err);
    }
}

void cmd_grep(int argc, char **argv, struct ShellIO *io) {
    if (argc < 2) return;
    char *keyword = argv[1];
    int key_len = strlen(keyword);
    
    int in_idx = 0;
    int out_idx = 0;
    char line[256];
    int line_idx = 0;
    
    while (io->input_buffer[in_idx] != '\0' && in_idx < MAX_IO_BUFFER) {
        char c = io->input_buffer[in_idx++];
        if (c != '\n' && line_idx < 255) {
            line[line_idx++] = c;
        } else {
            line[line_idx] = '\0';
            bool found = false;
            for (int i = 0; i <= line_idx - key_len; i++) {
                if (memcmp(&line[i], keyword, key_len) == 0) {
                    found = true;
                    break;
                }
            }
            if (found) {
                memcpy(io->output_buffer + out_idx, line, line_idx);
                out_idx += line_idx;
                io->output_buffer[out_idx++] = '\n';
            }
            line_idx = 0;
        }
    }
    io->output_buffer[out_idx] = '\0';
    io->output_len = out_idx;
}

// Code Fikri taro bawah yeah



// Code Athallah taro bawah yeah



// Code Atim taro bawah yeah


