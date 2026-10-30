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
void cmd_cp(int argc, char **argv, struct ShellIO *io, uint32_t cwd) {
    if (argc < 3) {
        char *err = "cp: Penggunaan: cp <src> <dst>\n";
        memcpy(io->output_buffer, err, strlen(err));
        io->output_len = strlen(err);
        return;
    }

    char file_buf[MAX_IO_BUFFER];
    memset(file_buf, 0, MAX_IO_BUFFER);

    struct EXT2DriverRequest read_req = {
        .parent_inode = cwd,
        .name = argv[1],
        .name_len = strlen(argv[1]),
        .buffer_size = MAX_IO_BUFFER,
        .buf = file_buf
    };

    int32_t retcode;
    syscall(SYSCALL_READ, (uint32_t) &read_req, (uint32_t) &retcode, 0);

    if (retcode != 0) {
        char *err = "cp: File sumber tidak ditemukan atau bukan file\n";
        memcpy(io->output_buffer, err, strlen(err));
        io->output_len = strlen(err);
        return;
    }

    // buffer_size == 0 berarti buat folder, jadi file kosong tetap ditulis minimal 1 byte
    uint32_t size = strlen(file_buf);
    if (size == 0) size = 1;

    struct EXT2DriverRequest write_req = {
        .parent_inode = cwd,
        .name = argv[2],
        .name_len = strlen(argv[2]),
        .is_directory = false,
        .buffer_size = size,
        .buf = file_buf
    };

    syscall(SYSCALL_WRITE, (uint32_t) &write_req, (uint32_t) &retcode, 0);

    if (retcode == 1) {
        char *err = "cp: File tujuan sudah ada\n";
        memcpy(io->output_buffer, err, strlen(err));
        io->output_len = strlen(err);
    } else if (retcode != 0) {
        char *err = "cp: Gagal menulis file tujuan\n";
        memcpy(io->output_buffer, err, strlen(err));
        io->output_len = strlen(err);
    }
}

void cmd_rm(int argc, char **argv, struct ShellIO *io, uint32_t cwd) {
    if (argc < 2) {
        char *err = "rm: Penggunaan: rm <file>\n";
        memcpy(io->output_buffer, err, strlen(err));
        io->output_len = strlen(err);
        return;
    }

    struct EXT2DriverRequest req = {
        .parent_inode = cwd,
        .name = argv[1],
        .name_len = strlen(argv[1]),
        .is_directory = false,
        .buffer_size = 0,
        .buf = 0
    };

    int32_t retcode;
    syscall(SYSCALL_DELETE, (uint32_t) &req, (uint32_t) &retcode, 0);

    if (retcode == 1) {
        char *err = "rm: File tidak ditemukan\n";
        memcpy(io->output_buffer, err, strlen(err));
        io->output_len = strlen(err);
    } else if (retcode != 0) {
        char *err = "rm: Gagal menghapus file\n";
        memcpy(io->output_buffer, err, strlen(err));
        io->output_len = strlen(err);
    }
}

void cmd_mv(int argc, char **argv, struct ShellIO *io, uint32_t cwd) {
    if (argc < 3) {
        char *err = "mv: Penggunaan: mv <src> <dst>\n";
        memcpy(io->output_buffer, err, strlen(err));
        io->output_len = strlen(err);
        return;
    }

    char file_buf[MAX_IO_BUFFER];
    memset(file_buf, 0, MAX_IO_BUFFER);

    struct EXT2DriverRequest read_req = {
        .parent_inode = cwd,
        .name = argv[1],
        .name_len = strlen(argv[1]),
        .buffer_size = MAX_IO_BUFFER,
        .buf = file_buf
    };

    int32_t retcode;
    syscall(SYSCALL_READ, (uint32_t) &read_req, (uint32_t) &retcode, 0);

    if (retcode != 0) {
        char *err = "mv: File sumber tidak ditemukan atau bukan file\n";
        memcpy(io->output_buffer, err, strlen(err));
        io->output_len = strlen(err);
        return;
    }

    uint32_t size = strlen(file_buf);
    if (size == 0) size = 1;

    struct EXT2DriverRequest write_req = {
        .parent_inode = cwd,
        .name = argv[2],
        .name_len = strlen(argv[2]),
        .is_directory = false,
        .buffer_size = size,
        .buf = file_buf
    };

    syscall(SYSCALL_WRITE, (uint32_t) &write_req, (uint32_t) &retcode, 0);

    if (retcode != 0) {
        char *err = (retcode == 1) ? "mv: File tujuan sudah ada\n" : "mv: Gagal menulis file tujuan\n";
        memcpy(io->output_buffer, err, strlen(err));
        io->output_len = strlen(err);
        return;
    }

    // Sumber baru dihapus setelah salinan berhasil dibuat
    struct EXT2DriverRequest del_req = {
        .parent_inode = cwd,
        .name = argv[1],
        .name_len = strlen(argv[1]),
        .is_directory = false,
        .buffer_size = 0,
        .buf = 0
    };

    syscall(SYSCALL_DELETE, (uint32_t) &del_req, (uint32_t) &retcode, 0);

    if (retcode != 0) {
        char *err = "mv: Gagal menghapus file sumber\n";
        memcpy(io->output_buffer, err, strlen(err));
        io->output_len = strlen(err);
    }
}


// Code Athallah taro bawah yeah
/* ------------------------------------ find ------------------------------------ *
 * find <nama>: cari file/folder bernama <nama> (nama harus persis sama) di SELURUH
 * file system, dimulai dari root (inode 1). Hasil: satu path lengkap per baris
 * (mis. /home/user/notes.txt), urutan DFS, ditulis ke io->output_buffer agar bisa
 * di-pipe ke grep. Jika tidak ada yang cocok, output kosong (seperti find di UNIX).
 *
 * EXT2 membentuk tree, jadi find = DFS pada tree itu lewat syscall read_directory.
 * Dibuat hemat memori karena shell berjalan di user mode dengan RAM kecil:
 *   - satu buffer directory dipakai bersama oleh semua tingkat rekursi
 *   - tidak ada array nama per tingkat: nama langsung ditulis ke satu buffer path
 *   - tidak memakai heap / variabel global (shell adalah flat binary)
 *   - kedalaman dibatasi FIND_MAX_DEPTH sebagai pengaman jika file system rusak
 * Karena buffer dipakai bersama, setelah selesai masuk ke sub-folder, directory
 * induknya dibaca ulang (1 syscall tambahan per sub-folder).
 * ------------------------------------------------------------------------------- */
#define FIND_DIR_BLOCKS 4      /* block tabel directory yang dibaca per folder (4 x 512 B) */
#define FIND_PATH_MAX   256    /* panjang maksimum path hasil (termasuk '\0') */
#define FIND_MAX_DEPTH  32     /* kedalaman folder maksimum yang ditelusuri */
#define FIND_ROOT_INODE 1
#define FIND_RESERVE    32     /* ruang output yang disisakan untuk pesan "terpotong" */
 
struct FindState {
    const char     *target;
    uint32_t        target_len;
    struct ShellIO *io;
    uint32_t        out_len;
    bool            stop;
    bool            truncated;
    char            path[FIND_PATH_MAX];                  /* path folder yang sedang dikunjungi */
    char            buf[FIND_DIR_BLOCKS * BLOCK_SIZE];    /* tabel directory, dipakai bersama */
};
 
/* Baca tabel directory milik dir_inode ke st->buf (trik yang sama dengan cmd_ls: entry "." di dir itu sendiri). */
static int8_t find_read_dir(struct FindState *st, uint32_t dir_inode) {
    memset(st->buf, 0, sizeof(st->buf));      /* sisa buffer harus nol agar daftar entry berakhir dengan benar */
 
    struct EXT2DriverRequest req = {
        .parent_inode = dir_inode,
        .name         = ".",
        .name_len     = 1,
        .buffer_size  = sizeof(st->buf),
        .buf          = st->buf
    };
 
    int32_t ret = -1;
    syscall(SYSCALL_READ_DIR, (uint32_t) &req, (uint32_t) &ret, 0);
    return (int8_t) ret;                      /* kernel mungkin hanya menulis 1 byte return code */
}
 
static bool find_is_dot_entry(const struct EXT2DirectoryEntry *e) {
    const char *n = (const char *) e + sizeof(struct EXT2DirectoryEntry);
    return (e->name_len == 1 && n[0] == '.') ||
           (e->name_len == 2 && n[0] == '.' && n[1] == '.');
}
 
/* Entry ke-n (mulai dari 0) selain "." dan "..", atau NULL jika sudah habis. Entry tidak melintasi batas block. */
static struct EXT2DirectoryEntry *find_nth_entry(char *buf, uint32_t n) {
    uint32_t seen = 0;
    for (uint32_t b = 0; b < FIND_DIR_BLOCKS; b++) {
        uint32_t off = 0;
        while (off + sizeof(struct EXT2DirectoryEntry) <= BLOCK_SIZE) {
            struct EXT2DirectoryEntry *e = (struct EXT2DirectoryEntry *) (buf + b * BLOCK_SIZE + off);
            if (e->inode == 0) break;                                          /* akhir daftar di block ini */
            if (e->rec_len < sizeof(struct EXT2DirectoryEntry) || off + e->rec_len > BLOCK_SIZE) break;   /* struktur rusak */
            if (!find_is_dot_entry(e)) {
                if (seen == n) return e;
                seen++;
            }
            off += e->rec_len;
        }
    }
    return NULL;
}
 
/* Tambah satu baris ke output. Berhenti (truncated) jika buffer output hampir penuh. */
static void find_emit(struct FindState *st, const char *line) {
    uint32_t len = strlen(line);
    if (st->out_len + len + 1 > MAX_IO_BUFFER - FIND_RESERVE - 1) {
        st->truncated = true;
        st->stop = true;
        return;
    }
    memcpy(st->io->output_buffer + st->out_len, line, len);
    st->out_len += len;
    st->io->output_buffer[st->out_len++] = '\n';
}
 
/* DFS: telusuri folder dir_inode. plen = panjang path folder ini di st->path ("" untuk root). */
static void find_walk(struct FindState *st, uint32_t dir_inode, uint32_t plen, uint32_t depth) {
    bool loaded = false;
 
    for (uint32_t n = 0; !st->stop; n++) {
        if (!loaded) {
            if (find_read_dir(st, dir_inode) != 0) return;
            loaded = true;
        }
 
        struct EXT2DirectoryEntry *e = find_nth_entry(st->buf, n);
        if (e == NULL) return;                                  /* semua entry folder ini sudah dikunjungi */
 
        const char *name = (const char *) e + sizeof(struct EXT2DirectoryEntry);
        uint32_t nlen  = e->name_len;
        uint32_t child = e->inode;
        bool is_dir    = (e->file_type == EXT2_FT_DIR);
 
        if (plen + 1 + nlen >= FIND_PATH_MAX) continue;         /* path terlalu panjang: lewati */
 
        st->path[plen] = '/';
        memcpy(st->path + plen + 1, name, nlen);
        st->path[plen + 1 + nlen] = '\0';
 
        if (nlen == st->target_len && memcmp(name, st->target, nlen) == 0)
            find_emit(st, st->path);
 
        if (is_dir && depth < FIND_MAX_DEPTH) {
            find_walk(st, child, plen + 1 + nlen, depth + 1);
            loaded = false;                                     /* st->buf sudah dipakai tingkat bawah */
        }
        st->path[plen] = '\0';                                  /* kembalikan path ke folder ini */
    }
}
 
void cmd_find(int argc, char **argv, struct ShellIO *io) {
    if (argc < 2) {
        const char *usage = "find: pemakaian: find <nama>\n";
        memcpy(io->output_buffer, usage, strlen(usage));
        io->output_len = strlen(usage);
        return;
    }
 
    struct FindState st;
    st.target     = argv[1];
    st.target_len = strlen(argv[1]);
    st.io         = io;
    st.out_len    = 0;
    st.stop       = false;
    st.truncated  = false;
    st.path[0]    = '\0';
 
    find_walk(&st, FIND_ROOT_INODE, 0, 0);
 
    if (st.truncated) {
        const char *note = "find: output terpotong\n";
        memcpy(io->output_buffer + st.out_len, note, strlen(note));
        st.out_len += strlen(note);
    }
    io->output_buffer[st.out_len] = '\0';
    io->output_len = st.out_len;
}


// Code Atim taro bawah yeah


