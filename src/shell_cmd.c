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

static int matchhere(char *regexp, char *text);
static int matchstar(int c, char *regexp, char *text);

static int match(char *regexp, char *text) {

    for (int i = 0; regexp[i] != '\0'; i++) {
        if (regexp[i] == '|') {
            regexp[i] = '\0'; 
            int match_left = match(regexp, text);
            regexp[i] = '|';  
            if (match_left) return 1;
            return match(regexp + i + 1, text); 
        }
    }

    if (regexp[0] == '^')
        return matchhere(regexp + 1, text);
    do {
        if (matchhere(regexp, text))
            return 1;
    } while (*text++ != '\0');
    return 0;
}

static int matchhere(char *regexp, char *text) {
    if (regexp[0] == '\0') return 1;

    if (regexp[0] == '\\' && regexp[1] != '\0') {
        if (*text != '\0' && *text == regexp[1])
            return matchhere(regexp + 2, text + 1);
        return 0;
    }

    if (regexp[0] == '[') {
        int negate = 0;
        int i = 1;
        if (regexp[i] == '^') { 
            negate = 1; 
            i++; 
        }
        
        int match_found = 0;
        while (regexp[i] != ']' && regexp[i] != '\0') {
            if (*text == regexp[i]) match_found = 1;
            i++;
        }
        
        if (regexp[i] == '\0') return 0; 
        
        if ((!negate && match_found) || (negate && !match_found)) {
            return matchhere(regexp + i + 1, text + 1);
        }
        return 0;
    }

    if (regexp[1] == '*') return matchstar(regexp[0], regexp + 2, text);
    
    if (regexp[1] == '+') {
        if (*text != '\0' && (*text == regexp[0] || regexp[0] == '.'))
            return matchstar(regexp[0], regexp + 2, text + 1);
        return 0;
    }
    
    if (regexp[1] == '?') {
        if (*text != '\0' && (*text == regexp[0] || regexp[0] == '.')) {
            if (matchhere(regexp + 2, text + 1)) return 1;
        }
        return matchhere(regexp + 2, text);
    }

    if (regexp[0] == '$' && regexp[1] == '\0') return *text == '\0';

    if (*text != '\0' && (regexp[0] == '.' || regexp[0] == *text))
        return matchhere(regexp + 1, text + 1);

    return 0;
}

static int matchstar(int c, char *regexp, char *text) {
    do {
        if (matchhere(regexp, text))
            return 1;
    } while (*text != '\0' && (*text++ == c || c == '.'));
    return 0;
}

void cmd_grep(int argc, char **argv, struct ShellIO *io) {
    if (argc < 2) return;
    char *keyword = argv[1]; 
    
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
            
            if (match(keyword, line)) {
                memcpy(io->output_buffer + out_idx, line, line_idx);
                out_idx += line_idx;
                io->output_buffer[out_idx++] = '\n';
            }
        }
    }
    
    io->output_buffer[out_idx] = '\0';
    io->output_len = out_idx;
}

// Code Fikri taro bawah yeah



// Code Athallah taro bawah yeah



// Code Atim taro bawah yeah


