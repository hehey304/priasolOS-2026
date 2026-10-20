#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/stdlib/string.h"
#include "header/filesystem/ext2.h"

const uint8_t fs_signature[BLOCK_SIZE] = {
    'C', 'o', 'u', 'r', 's', 'e', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',  ' ',
    'D', 'e', 's', 'i', 'g', 'n', 'e', 'd', ' ', 'b', 'y', ' ', ' ', ' ', ' ',  ' ',
    'L', 'a', 'b', ' ', 'S', 'i', 's', 't', 'e', 'r', ' ', 'I', 'T', 'B', ' ',  ' ',
    'M', 'a', 'd', 'e', ' ', 'w', 'i', 't', 'h', ' ', '<', '3', ' ', ' ', ' ',  ' ',
    '-', '-', '-', '-', '-', '-', '-', '-', '-', '-', '-', '2', '0', '2', '5', '\n',
    [BLOCK_SIZE-2] = 'O',
    [BLOCK_SIZE-1] = 'k',
};

/* SEMENTARA (bagian 2.5 / P1): isi variabel ini oleh initialize_filesystem_ext2().
 * Hapus deklarasi ini jika versi P1 sudah ada di file yang sama. */
static struct EXT2BlockGroupDescriptorTable bgd_table;

#define PTRS_PER_BLOCK (BLOCK_SIZE / sizeof(uint32_t))   /* 128 pointer per block */

/* ======================= helper parsing directory entry ======================= *
 * Layout satu entry (packed):  inode(4) rec_len(2) name_len(2) file_type(1) name[name_len]
 * Header 9 byte, lalu nama, lalu padding sampai kelipatan 4. rec_len = jarak ke entry
 * berikutnya; entry terakhir di block memiliki rec_len sampai batas akhir block.
 * Entry tidak boleh melintasi batas block. inode == 0 berarti slot kosong.
 * Akhir daftar: entry pertama ber-inode 0 (sisa block boleh berisi nol atau sampah, seperti pada
 * sample-image.bin); rec_len entry terakhir boleh pas atau memanjang sampai akhir block.
 * ============================================================================== */

char *get_entry_name(void *entry) {
    return (char *) entry + sizeof(struct EXT2DirectoryEntry);
}

struct EXT2DirectoryEntry *get_directory_entry(void *ptr, uint32_t offset) {
    return (struct EXT2DirectoryEntry *) ((uint8_t *) ptr + offset);
}

struct EXT2DirectoryEntry *get_next_directory_entry(struct EXT2DirectoryEntry *entry) {
    return (struct EXT2DirectoryEntry *) ((uint8_t *) entry + entry->rec_len);
}

/* panjang minimum satu entry: header + nama, dibulatkan ke atas ke kelipatan 4 */
uint16_t get_entry_record_len(uint8_t name_len) {
    return (uint16_t) ((sizeof(struct EXT2DirectoryEntry) + name_len + 3u) & ~3u);
}

/* offset entry pertama setelah "." dan ".." */
uint32_t get_dir_first_child_offset(void *ptr) {
    struct EXT2DirectoryEntry *dot    = get_directory_entry(ptr, 0);
    struct EXT2DirectoryEntry *dotdot = get_next_directory_entry(dot);
    return (uint32_t) dot->rec_len + dotdot->rec_len;
}

static bool entry_is_valid_at(const struct EXT2DirectoryEntry *e, uint32_t off) {
    return e->rec_len >= sizeof(struct EXT2DirectoryEntry) && off + e->rec_len <= BLOCK_SIZE;
}

static bool entry_name_is(struct EXT2DirectoryEntry *e, const char *name, uint8_t name_len) {
    return e->name_len == name_len && memcmp(get_entry_name(e), name, name_len) == 0;
}

static bool entry_is_dot_or_dotdot(struct EXT2DirectoryEntry *e) {
    char *n = get_entry_name(e);
    return (e->name_len == 1 && n[0] == '.') || (e->name_len == 2 && n[0] == '.' && n[1] == '.');
}

/* ======================== nomor inode -> lokasi di disk ======================== */

uint32_t inode_to_bgd(uint32_t inode) {
    return (inode - 1) / INODES_PER_GROUP;
}

uint32_t inode_to_local(uint32_t inode) {
    return (inode - 1) % INODES_PER_GROUP;
}

/* SEMENTARA (P2): versi lokal read_inode & inode_block_at.
 * Hapus keduanya saat versi P2 masuk dan sesuaikan pemanggilnya. */

/* Baca 1 inode dari inode table. false jika nomor inode di luar jangkauan. */
static bool read_inode(uint32_t inode, struct EXT2Inode *out) {
    if (inode == 0 || inode > (uint32_t) (INODES_PER_GROUP * GROUPS_COUNT))
        return false;

    uint32_t bgd   = inode_to_bgd(inode);
    uint32_t local = inode_to_local(inode);
    uint32_t lba   = bgd_table.table[bgd].bg_inode_table + local / INODES_PER_TABLE;

    struct BlockBuffer b;
    read_blocks(&b, lba, 1);
    memcpy(out, b.buf + (local % INODES_PER_TABLE) * sizeof(struct EXT2Inode),
           sizeof(struct EXT2Inode));
    return true;
}

/* Cek inode directory. SESUAIKAN dengan nilai i_mode yang diisi init_directory_table()/write()
 * (spek menyebut EXT2_FT_DIR, kit menyediakan EXT2_S_IFDIR). */
static bool inode_is_dir(const struct EXT2Inode *n) {
    return (n->i_mode & EXT2_S_IFDIR) != 0 || n->i_mode == EXT2_FT_DIR;
}

static uint32_t ptr_at(uint32_t block, uint32_t idx) {
    struct BlockBuffer b;
    if (block == 0) return 0;
    read_blocks(&b, block, 1);
    return ((uint32_t *) b.buf)[idx];
}

/* @return LBA block data ke-`idx` milik node (direct, indirect, doubly, triply); 0 jika tidak ada */
static uint32_t inode_block_at(const struct EXT2Inode *node, uint32_t idx) {
    if (idx < 12)
        return node->i_block[idx];
    idx -= 12;

    if (idx < PTRS_PER_BLOCK)                                  /* indirect */
        return ptr_at(node->i_block[12], idx);
    idx -= PTRS_PER_BLOCK;

    if (idx < PTRS_PER_BLOCK * PTRS_PER_BLOCK) {               /* doubly indirect */
        uint32_t l1 = ptr_at(node->i_block[13], idx / PTRS_PER_BLOCK);
        return ptr_at(l1, idx % PTRS_PER_BLOCK);
    }
    idx -= PTRS_PER_BLOCK * PTRS_PER_BLOCK;

    uint32_t l1 = ptr_at(node->i_block[14], idx / (PTRS_PER_BLOCK * PTRS_PER_BLOCK)); /* triply */
    uint32_t l2 = ptr_at(l1, (idx / PTRS_PER_BLOCK) % PTRS_PER_BLOCK);
    return ptr_at(l2, idx % PTRS_PER_BLOCK);
}

/* ============================ directory layer (P3) ============================ *
 * Model block directory (sesuai sample-image.bin dari kit):
 *   - entry tersusun rapat dari offset 0, ditelusuri lewat rec_len;
 *   - daftar BERAKHIR pada entry pertama ber-inode 0 (sisa block diabaikan, boleh berisi
 *     sampah/nol), atau jika sisa block tidak cukup untuk satu header;
 *   - inode 0 TIDAK dipakai sebagai "slot kosong" di tengah daftar.
 * rec_len entry terakhir boleh sama dengan ukurannya sendiri atau memanjang sampai akhir block.
 * ============================================================================== */

bool ext2_dir_lookup(const struct EXT2Inode *dir, const char *name, uint8_t name_len,
                     uint32_t *found_inode, uint8_t *found_type) {
    for (uint32_t i = 0; ; i++) {
        uint32_t lba = inode_block_at(dir, i);
        if (lba == 0) return false;                      /* block directory habis */

        struct BlockBuffer b;
        read_blocks(&b, lba, 1);

        uint32_t off = 0;
        while (off + sizeof(struct EXT2DirectoryEntry) <= BLOCK_SIZE) {
            struct EXT2DirectoryEntry *e = get_directory_entry(b.buf, off);
            if (e->inode == 0) break;                    /* akhir daftar di block ini */
            if (!entry_is_valid_at(e, off)) break;       /* proteksi struktur rusak */

            if (entry_name_is(e, name, name_len)) {
                *found_inode = e->inode;
                *found_type  = e->file_type;
                return true;
            }
            off += e->rec_len;
        }
    }
}

bool ext2_dir_is_empty(uint32_t dir_inode) {
    struct EXT2Inode dir;
    if (!read_inode(dir_inode, &dir) || !inode_is_dir(&dir))
        return false;

    for (uint32_t i = 0; ; i++) {
        uint32_t lba = inode_block_at(&dir, i);
        if (lba == 0) return true;

        struct BlockBuffer b;
        read_blocks(&b, lba, 1);

        uint32_t off = 0;
        while (off + sizeof(struct EXT2DirectoryEntry) <= BLOCK_SIZE) {
            struct EXT2DirectoryEntry *e = get_directory_entry(b.buf, off);
            if (e->inode == 0) break;
            if (!entry_is_valid_at(e, off)) break;
            if (!entry_is_dot_or_dotdot(e))
                return false;
            off += e->rec_len;
        }
    }
}

int8_t ext2_dir_add(uint32_t dir_inode, const char *name, uint8_t name_len,
                    uint32_t child_inode, uint8_t file_type) {
    struct EXT2Inode dir;
    if (name_len == 0 || !read_inode(dir_inode, &dir) || !inode_is_dir(&dir))
        return 2;

    uint32_t dummy_ino; uint8_t dummy_type;
    if (ext2_dir_lookup(&dir, name, name_len, &dummy_ino, &dummy_type))
        return 1;

    uint16_t need = get_entry_record_len(name_len);

    for (uint32_t i = 0; ; i++) {
        uint32_t lba = inode_block_at(&dir, i);
        if (lba == 0) return 3;                          /* tidak ada ruang: butuh block baru */

        struct BlockBuffer b;
        read_blocks(&b, lba, 1);

        uint32_t off = 0;
        while (off + sizeof(struct EXT2DirectoryEntry) <= BLOCK_SIZE) {
            struct EXT2DirectoryEntry *e = get_directory_entry(b.buf, off);

            if (e->inode == 0) {                         /* akhir daftar: tulis entry baru di sini */
                if (off + need > BLOCK_SIZE) break;      /* tidak muat di block ini */
                memset(b.buf + off, 0, BLOCK_SIZE - off);/* bersihkan sisa block (sampah / data lama) */
                e->inode     = child_inode;
                e->rec_len   = need;
                e->name_len  = name_len;
                e->file_type = file_type;
                memcpy(get_entry_name(e), name, name_len);
                write_blocks(&b, lba, 1);
                return 0;
            }
            if (!entry_is_valid_at(e, off)) break;

            /* entry yang rec_len-nya punya sisa ruang cukup: pecah, entry baru menempati sisanya */
            uint16_t used = get_entry_record_len((uint8_t) e->name_len);
            if (e->rec_len >= used + need) {
                uint16_t slot = (uint16_t) (e->rec_len - used);
                e->rec_len = used;
                struct EXT2DirectoryEntry *n = get_directory_entry(b.buf, off + used);
                memset(n, 0, slot);
                n->inode     = child_inode;
                n->rec_len   = slot;
                n->name_len  = name_len;
                n->file_type = file_type;
                memcpy(get_entry_name(n), name, name_len);
                write_blocks(&b, lba, 1);
                return 0;
            }
            off += e->rec_len;
        }
    }
}

int8_t ext2_dir_remove(uint32_t dir_inode, const char *name, uint8_t name_len) {
    struct EXT2Inode dir;
    if (!read_inode(dir_inode, &dir) || !inode_is_dir(&dir))
        return 2;
    if ((name_len == 1 && name[0] == '.') || (name_len == 2 && name[0] == '.' && name[1] == '.'))
        return 3;                                        /* "." dan ".." tidak boleh dihapus */

    for (uint32_t i = 0; ; i++) {
        uint32_t lba = inode_block_at(&dir, i);
        if (lba == 0) return 1;

        struct BlockBuffer b;
        read_blocks(&b, lba, 1);

        struct EXT2DirectoryEntry *prev = NULL;
        uint32_t off = 0;
        while (off + sizeof(struct EXT2DirectoryEntry) <= BLOCK_SIZE) {
            struct EXT2DirectoryEntry *e = get_directory_entry(b.buf, off);
            if (e->inode == 0) break;
            if (!entry_is_valid_at(e, off)) break;

            if (entry_name_is(e, name, name_len)) {
                uint16_t len = e->rec_len;
                if (prev) {                              /* gabungkan ruangnya ke entry sebelumnya */
                    memset(e, 0, len);
                    prev->rec_len = (uint16_t) (prev->rec_len + len);
                } else {                                 /* entry pertama di block: geser sisa daftar ke kiri */
                    memmove(b.buf + off, b.buf + off + len, BLOCK_SIZE - off - len);
                    memset(b.buf + BLOCK_SIZE - len, 0, len);
                }
                write_blocks(&b, lba, 1);
                return 0;
            }
            prev = e;
            off += e->rec_len;
        }
    }
}

/* deklarasi lama di ext2.h: true jika directory tidak punya anak */
bool is_directory_empty(uint32_t inode) {
    return ext2_dir_is_empty(inode);
}

/* ================================ read() 2.6.1 ================================= */

int8_t read(struct EXT2DriverRequest request) {
    struct EXT2Inode parent;
    if (!read_inode(request.parent_inode, &parent) || !inode_is_dir(&parent))
        return 4;                                        /* parent invalid */

    uint32_t ino;
    uint8_t  type;
    if (!ext2_dir_lookup(&parent, request.name, request.name_len, &ino, &type))
        return 3;                                        /* tidak ditemukan */
    if (type != EXT2_FT_REG_FILE)
        return 1;                                        /* bukan file */

    struct EXT2Inode node;
    if (!read_inode(ino, &node))
        return -1;
    if (request.buffer_size < node.i_size)
        return 2;                                        /* buffer tidak cukup */

    uint8_t *out = (uint8_t *) request.buf;
    for (uint32_t i = 0; i * BLOCK_SIZE < node.i_size; i++) {
        uint32_t lba = inode_block_at(&node, i);
        if (lba == 0) return -1;                         /* pointer rusak */

        struct BlockBuffer b;
        read_blocks(&b, lba, 1);
        uint32_t left = node.i_size - i * BLOCK_SIZE;
        memcpy(out + i * BLOCK_SIZE, b.buf, left < BLOCK_SIZE ? left : BLOCK_SIZE);
    }
    return 0;
}

/* ============================= read_directory() ================================ */

int8_t read_directory(struct EXT2DriverRequest *prequest) {
    struct EXT2Inode parent;
    if (!read_inode(prequest->parent_inode, &parent) || !inode_is_dir(&parent))
        return 3;                                        /* parent invalid */

    uint32_t ino;
    uint8_t  type;
    if (!ext2_dir_lookup(&parent, prequest->name, prequest->name_len, &ino, &type))
        return 2;                                        /* tidak ditemukan */
    if (type != EXT2_FT_DIR)
        return 1;                                        /* bukan folder */

    struct EXT2Inode node;
    if (!read_inode(ino, &node))
        return -1;

    /* salin tiap block directory table (list EXT2DirectoryEntry) ke buf selama masih muat */
    uint8_t *out = (uint8_t *) prequest->buf;
    for (uint32_t i = 0; (i + 1) * BLOCK_SIZE <= prequest->buffer_size; i++) {
        uint32_t lba = inode_block_at(&node, i);
        if (lba == 0) break;
        read_blocks(out + i * BLOCK_SIZE, lba, 1);
    }
    return 0;
}