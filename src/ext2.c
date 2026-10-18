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

static struct EXT2BlockGroupDescriptorTable bgd_table;

#define PTRS_PER_BLOCK (BLOCK_SIZE / sizeof(uint32_t))   /* 128 */
 
/* ---------- helper directory entry (lewati yang sudah ada di ext2.c) ---------- */
char *get_entry_name(void *entry) {
    return (char *) entry + sizeof(struct EXT2DirectoryEntry);
}
 
struct EXT2DirectoryEntry *get_directory_entry(void *ptr, uint32_t offset) {
    return (struct EXT2DirectoryEntry *) ((uint8_t *) ptr + offset);
}
 
/* ---------- nomor inode -> lokasi ---------- */
uint32_t inode_to_bgd(uint32_t inode) {
    return (inode - 1) / INODES_PER_GROUP;
}
 
uint32_t inode_to_local(uint32_t inode) {
    return (inode - 1) % INODES_PER_GROUP;
}
 
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
 * oleh anggota yang mengerjakan 2.6.2 (spek menyebut EXT2_FT_DIR, kit menyediakan EXT2_S_IFDIR). */
static bool inode_is_dir(const struct EXT2Inode *n) {
    return (n->i_mode & EXT2_S_IFDIR) != 0 || n->i_mode == EXT2_FT_DIR;
}
 
/* ---------- logical block index -> LBA, lewat direct/indirect/doubly/triply ---------- */
static uint32_t ptr_at(uint32_t block, uint32_t idx) {
    struct BlockBuffer b;
    if (block == 0) return 0;
    read_blocks(&b, block, 1);
    return ((uint32_t *) b.buf)[idx];
}
 
/* @return LBA block data ke-`idx` milik node, 0 jika tidak ada */
static uint32_t node_block_at(const struct EXT2Inode *node, uint32_t idx) {
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
 
/* Cari `name` di directory `dir`. Mengisi *found_inode & *found_type jika ketemu. */
static bool find_entry(const struct EXT2Inode *dir, const char *name, uint8_t name_len,
                       uint32_t *found_inode, uint8_t *found_type) {
    for (uint32_t i = 0; ; i++) {
        uint32_t lba = node_block_at(dir, i);
        if (lba == 0) return false;                 /* block habis */
 
        struct BlockBuffer b;
        read_blocks(&b, lba, 1);
 
        uint32_t off = 0;
        while (off + sizeof(struct EXT2DirectoryEntry) <= BLOCK_SIZE) {
            struct EXT2DirectoryEntry *e = get_directory_entry(b.buf, off);
            if (e->rec_len == 0) break;             /* proteksi loop tak hingga */
 
            if (e->inode != 0 && e->name_len == name_len &&
                memcmp(get_entry_name(e), name, name_len) == 0) {
                *found_inode = e->inode;
                *found_type  = e->file_type;
                return true;
            }
            off += e->rec_len;
        }
    }
}
 
/* ============================ read() ============================ */
int8_t read(struct EXT2DriverRequest request) {
    struct EXT2Inode parent;
    if (!read_inode(request.parent_inode, &parent) || !inode_is_dir(&parent))
        return 4;                                   /* parent invalid */
 
    uint32_t ino;
    uint8_t  type;
    if (!find_entry(&parent, request.name, request.name_len, &ino, &type))
        return 3;                                   /* tidak ditemukan */
    if (type != EXT2_FT_REG_FILE)
        return 1;                                   /* bukan file */
 
    struct EXT2Inode node;
    if (!read_inode(ino, &node))
        return -1;
    if (request.buffer_size < node.i_size)
        return 2;                                   /* buffer tidak cukup */
 
    uint8_t *out = (uint8_t *) request.buf;
    for (uint32_t i = 0; i * BLOCK_SIZE < node.i_size; i++) {
        uint32_t lba = node_block_at(&node, i);
        if (lba == 0) return -1;                    /* pointer rusak */
 
        struct BlockBuffer b;
        read_blocks(&b, lba, 1);
        uint32_t left = node.i_size - i * BLOCK_SIZE;
        memcpy(out + i * BLOCK_SIZE, b.buf, left < BLOCK_SIZE ? left : BLOCK_SIZE);
    }
    return 0;
}
 
/* ======================== read_directory() ======================= */
int8_t read_directory(struct EXT2DriverRequest *prequest) {
    struct EXT2Inode parent;
    if (!read_inode(prequest->parent_inode, &parent) || !inode_is_dir(&parent))
        return 3;                                   /* parent invalid */
 
    uint32_t ino;
    uint8_t  type;
    if (!find_entry(&parent, prequest->name, prequest->name_len, &ino, &type))
        return 2;                                   /* tidak ditemukan */
    if (type != EXT2_FT_DIR)
        return 1;                                   /* bukan folder */
 
    struct EXT2Inode node;
    if (!read_inode(ino, &node))
        return -1;
 
    /* Salin tiap block directory table (list EXT2DirectoryEntry) ke buf. */
    uint8_t *out = (uint8_t *) prequest->buf;
    for (uint32_t i = 0; (i + 1) * BLOCK_SIZE <= prequest->buffer_size; i++) {
        uint32_t lba = node_block_at(&node, i);
        if (lba == 0) break;
        read_blocks(out + i * BLOCK_SIZE, lba, 1);
    }
    return 0;
}