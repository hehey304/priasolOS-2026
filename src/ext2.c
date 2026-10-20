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
static struct EXT2Superblock sblock;

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

/* ======================== INITIALIZATION (P2) ======================= */
bool is_empty_storage(void) {
    struct BlockBuffer b;
    read_blocks(&b, BOOT_SECTOR, 1);
    return memcmp(b.buf, fs_signature, BLOCK_SIZE) != 0;
}

void create_ext2(void) {
    write_blocks((void *)fs_signature, BOOT_SECTOR, 1);

    sblock.s_inodes_count = INODES_PER_GROUP * GROUPS_COUNT;
    sblock.s_blocks_count = DISK_SPACE / BLOCK_SIZE;
    sblock.s_magic = EXT2_SUPER_MAGIC;
    sblock.s_blocks_per_group = BLOCKS_PER_GROUP;
    sblock.s_inodes_per_group = INODES_PER_GROUP;
    sblock.s_first_data_block = 1; 

    struct BlockBuffer sb_buf = {0};
    memcpy(sb_buf.buf, &sblock, sizeof(sblock));
    write_blocks(&sb_buf, 1, 1);
    
    uint32_t current_lba = 3; 

    for (uint32_t i = 0; i < GROUPS_COUNT; i++) {
        bgd_table.table[i].bg_block_bitmap = current_lba;
        current_lba += 1; 
        
        bgd_table.table[i].bg_inode_bitmap = current_lba;
        current_lba += 1; 
        
        bgd_table.table[i].bg_inode_table = current_lba;
        current_lba += INODES_TABLE_BLOCK_COUNT; 
        
        bgd_table.table[i].bg_free_inodes_count = INODES_PER_GROUP;
        bgd_table.table[i].bg_free_blocks_count = BLOCKS_PER_GROUP;
        bgd_table.table[i].bg_used_dirs_count = 0;
    }

    struct BlockBuffer empty_block = {0}; 

    for (uint32_t i = 0; i < GROUPS_COUNT; i++) {
        write_blocks(&empty_block, bgd_table.table[i].bg_block_bitmap, 1);
        write_blocks(&empty_block, bgd_table.table[i].bg_inode_bitmap, 1);
        for (uint32_t j = 0; j < INODES_TABLE_BLOCK_COUNT; j++) {
            write_blocks(&empty_block, bgd_table.table[i].bg_inode_table + j, 1);
        }
    }

    struct BlockBuffer inode_bitmap_buf;
    read_blocks(&inode_bitmap_buf, bgd_table.table[0].bg_inode_bitmap, 1);
    
    inode_bitmap_buf.buf[0] |= (1 << 0);
    write_blocks(&inode_bitmap_buf, bgd_table.table[0].bg_inode_bitmap, 1);

    struct EXT2Inode root_inode = {0};
    root_inode.i_mode = EXT2_FT_DIR; 
    root_inode.i_size = BLOCK_SIZE;  
    root_inode.i_blocks = 1;         
    
    uint32_t root_data_lba = current_lba;
    root_inode.i_block[0] = root_data_lba;

    struct BlockBuffer inode_table_buf;
    read_blocks(&inode_table_buf, bgd_table.table[0].bg_inode_table, 1);
    
    memcpy(inode_table_buf.buf, &root_inode, sizeof(struct EXT2Inode));
    write_blocks(&inode_table_buf, bgd_table.table[0].bg_inode_table, 1);

    struct BlockBuffer root_data_buf = {0};
    
    struct EXT2DirectoryEntry *dot = (struct EXT2DirectoryEntry *) root_data_buf.buf;
    dot->inode = 1;
    dot->file_type = EXT2_FT_DIR; 
    dot->name_len = 1;
    dot->rec_len = 12; 
    get_entry_name(dot)[0] = '.'; 

    struct EXT2DirectoryEntry *dotdot = (struct EXT2DirectoryEntry *) (root_data_buf.buf + dot->rec_len);
    dotdot->inode = 1;
    dotdot->file_type = EXT2_FT_DIR; 
    dotdot->name_len = 2;
    dotdot->rec_len = BLOCK_SIZE - dot->rec_len; 
    get_entry_name(dotdot)[0] = '.';
    get_entry_name(dotdot)[1] = '.';

    write_blocks(&root_data_buf, root_data_lba, 1);

    struct BlockBuffer block_bitmap_buf = {0};
    for (uint32_t blk = 0; blk <= root_data_lba; blk++)
        block_bitmap_buf.buf[blk / 8] |= (1 << (blk % 8));
    write_blocks(&block_bitmap_buf, bgd_table.table[0].bg_block_bitmap, 1);

    bgd_table.table[0].bg_free_inodes_count -= 1;
    bgd_table.table[0].bg_free_blocks_count -= (root_data_lba + 1);
    bgd_table.table[0].bg_used_dirs_count   = 1;

    struct BlockBuffer bgd_buf = {0};
    memcpy(bgd_buf.buf, &bgd_table, sizeof(bgd_table));
    write_blocks(&bgd_buf, 2, 1);
}

void initialize_filesystem_ext2(void) {
    if (is_empty_storage()) {
        create_ext2();
    } else {
        struct BlockBuffer b;
        read_blocks(&b, 1, 1);
        memcpy(&sblock, b.buf, sizeof(sblock));
        read_blocks(&b, 2, 1);
        memcpy(&bgd_table, b.buf, sizeof(bgd_table));
    }
}

/* ============================ write() ============================ */

static uint32_t p4_find_free_inode() {
    struct BlockBuffer bitmap_buf;
    for (uint32_t bg = 0; bg < GROUPS_COUNT; bg++) {
        if (bgd_table.table[bg].bg_free_inodes_count > 0) {
            read_blocks(&bitmap_buf, bgd_table.table[bg].bg_inode_bitmap, 1);
            for (uint32_t i = 0; i < BLOCK_SIZE; i++) {
                if (bitmap_buf.buf[i] != 0xFF) { 
                    for (uint8_t bit = 0; bit < 8; bit++) {
                        if (!(bitmap_buf.buf[i] & (1 << bit))) {
                            bitmap_buf.buf[i] |= (1 << bit);
                            write_blocks(&bitmap_buf, bgd_table.table[bg].bg_inode_bitmap, 1);
                            
                            bgd_table.table[bg].bg_free_inodes_count--;
                            struct BlockBuffer bgd_buf;
                            memcpy(bgd_buf.buf, &bgd_table, sizeof(bgd_table));
                            write_blocks(&bgd_buf, 2, 1);

                            return (bg * INODES_PER_GROUP) + (i * 8) + bit + 1;
                        }
                    }
                }
            }
        }
    }
    return 0; 
}

static uint32_t p4_find_free_block() {
    struct BlockBuffer bitmap_buf;
    for (uint32_t bg = 0; bg < GROUPS_COUNT; bg++) {
        if (bgd_table.table[bg].bg_free_blocks_count > 0) {
            read_blocks(&bitmap_buf, bgd_table.table[bg].bg_block_bitmap, 1);
            for (uint32_t i = 0; i < BLOCK_SIZE; i++) {
                if (bitmap_buf.buf[i] != 0xFF) { 
                    for (uint8_t bit = 0; bit < 8; bit++) {
                        if (!(bitmap_buf.buf[i] & (1 << bit))) {
                            bitmap_buf.buf[i] |= (1 << bit);
                            write_blocks(&bitmap_buf, bgd_table.table[bg].bg_block_bitmap, 1);

                            bgd_table.table[bg].bg_free_blocks_count--;
                            struct BlockBuffer bgd_buf;
                            memcpy(bgd_buf.buf, &bgd_table, sizeof(bgd_table));
                            write_blocks(&bgd_buf, 2, 1);

                            uint32_t lba = (bg * BLOCKS_PER_GROUP) + (i * 8) + bit;
                            return lba;
                        }
                    }
                }
            }
        }
    }
    return 0; 
}

static void p4_write_inode(uint32_t inode_num, struct EXT2Inode *inode_data) {
    uint32_t bgd = inode_to_bgd(inode_num);
    uint32_t local = inode_to_local(inode_num);
    uint32_t lba = bgd_table.table[bgd].bg_inode_table + (local / INODES_PER_TABLE);
    uint32_t offset = (local % INODES_PER_TABLE) * sizeof(struct EXT2Inode);

    struct BlockBuffer b;
    read_blocks(&b, lba, 1);
    memcpy(b.buf + offset, inode_data, sizeof(struct EXT2Inode));
    write_blocks(&b, lba, 1);
}

static bool p4_check_duplicate(const struct EXT2Inode *dir, const char *name, uint8_t name_len) {
    for (uint32_t i = 0; i < 12; i++) { 
        uint32_t lba = dir->i_block[i];
        if (lba == 0) continue; 

        struct BlockBuffer b;
        read_blocks(&b, lba, 1);

        uint32_t off = 0;
        while (off < BLOCK_SIZE) {
            struct EXT2DirectoryEntry *e = (struct EXT2DirectoryEntry *)(b.buf + off);
            
            if (e->rec_len == 0) {
                break; 
            }

            if (e->inode != 0 && e->name_len == name_len && memcmp((char*)e + sizeof(struct EXT2DirectoryEntry), name, name_len) == 0) {
                return true; 
            }
            
            off += e->rec_len;
        }
    }
    return false; 
}

int8_t write(struct EXT2DriverRequest *request) {
    struct EXT2Inode parent_inode;
    if (!read_inode(request->parent_inode, &parent_inode)) {
        return 2; 
    }
    
    if ((parent_inode.i_mode & EXT2_S_IFDIR) == 0 && parent_inode.i_mode != EXT2_FT_DIR) {
        return 2; 
    }

    if (p4_check_duplicate(&parent_inode, request->name, request->name_len)) {
        return 1; 
    }

    uint32_t new_inode_num = p4_find_free_inode();
    if (new_inode_num == 0) return -1; 

    struct EXT2Inode new_inode = {0};

    if (request->is_directory || request->buffer_size == 0) {
        new_inode.i_mode = EXT2_FT_DIR; 
        new_inode.i_size = BLOCK_SIZE;
        new_inode.i_blocks = 1;
        
        uint32_t new_dir_block = p4_find_free_block();
        if (new_dir_block == 0) return -1;
        new_inode.i_block[0] = new_dir_block;

        struct BlockBuffer dir_buf = {0}; 
        
        struct EXT2DirectoryEntry *dot = (struct EXT2DirectoryEntry *) dir_buf.buf;
        dot->inode = new_inode_num;
        dot->file_type = EXT2_FT_DIR;
        dot->name_len = 1;
        dot->rec_len = 12; 
        get_entry_name(dot)[0] = '.';

        struct EXT2DirectoryEntry *dotdot = (struct EXT2DirectoryEntry *) (dir_buf.buf + dot->rec_len);
        dotdot->inode = request->parent_inode;
        dotdot->file_type = EXT2_FT_DIR;
        dotdot->name_len = 2;
        dotdot->rec_len = BLOCK_SIZE - dot->rec_len; 
        get_entry_name(dotdot)[0] = '.';
        get_entry_name(dotdot)[1] = '.';

        write_blocks(&dir_buf, new_dir_block, 1);
        
        uint32_t bgd_idx = inode_to_bgd(new_inode_num);
        bgd_table.table[bgd_idx].bg_used_dirs_count++;
        
    } else {
        new_inode.i_mode = EXT2_FT_REG_FILE; 
        new_inode.i_size = request->buffer_size;
        
        uint32_t blocks_needed = (request->buffer_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
        if (blocks_needed == 0) blocks_needed = 1; 
        new_inode.i_blocks = blocks_needed;

        if (blocks_needed > 12) {
            return -1; 
        }

        uint8_t *data_ptr = (uint8_t *)request->buf;
        for (uint32_t i = 0; i < blocks_needed; i++) {
            uint32_t new_data_block = p4_find_free_block();
            if (new_data_block == 0) return -1;
            
            new_inode.i_block[i] = new_data_block;

            struct BlockBuffer data_buf = {0}; 
            
            uint32_t remaining = request->buffer_size > (i * BLOCK_SIZE) ? request->buffer_size - (i * BLOCK_SIZE) : 0;
            uint32_t copy_size = (remaining > BLOCK_SIZE) ? BLOCK_SIZE : remaining;
            
            if (copy_size > 0 && data_ptr != NULL) {
                memcpy(data_buf.buf, data_ptr + (i * BLOCK_SIZE), copy_size);
            }
            write_blocks(&data_buf, new_data_block, 1);
        }
    }

    p4_write_inode(new_inode_num, &new_inode);

    bool entry_added = false;

    uint16_t required_len = sizeof(struct EXT2DirectoryEntry) + request->name_len;
    if (required_len % 4 != 0) required_len += 4 - (required_len % 4); 

    struct BlockBuffer p_dir_buf;
    for (uint32_t i = 0; i < 12; i++) {
        uint32_t p_lba = parent_inode.i_block[i];
        if (p_lba == 0) {
            uint32_t new_parent_block = p4_find_free_block();
            if (new_parent_block == 0) return -1;
            parent_inode.i_block[i] = new_parent_block;
            parent_inode.i_blocks++;
            
            struct BlockBuffer new_p_dir_buf = {0};
            struct EXT2DirectoryEntry *new_entry = (struct EXT2DirectoryEntry *)new_p_dir_buf.buf;
            new_entry->inode = new_inode_num;
            new_entry->name_len = request->name_len;
            new_entry->file_type = request->is_directory ? EXT2_FT_DIR : EXT2_FT_REG_FILE;
            new_entry->rec_len = BLOCK_SIZE; 
            memset(get_entry_name(new_entry), 0, new_entry->name_len + 4); 
            memcpy(get_entry_name(new_entry), request->name, request->name_len);
            
            write_blocks(&new_p_dir_buf, new_parent_block, 1);
            entry_added = true;
            break; 
        }

        read_blocks(&p_dir_buf, p_lba, 1);
        uint32_t offset = 0;
        
        while (offset < BLOCK_SIZE) {
            struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *)(p_dir_buf.buf + offset);
            
            if (entry->rec_len == 0) break;

            uint16_t ideal_len = sizeof(struct EXT2DirectoryEntry) + entry->name_len;
            if (ideal_len % 4 != 0) ideal_len += 4 - (ideal_len % 4);

            if (entry->inode == 0 || (entry->rec_len - ideal_len) >= required_len) {
                uint16_t old_rec_len = entry->rec_len;
                
                if (entry->inode != 0) {
                    entry->rec_len = ideal_len; 
                    entry = (struct EXT2DirectoryEntry *)((uint8_t *)entry + ideal_len); 
                    entry->rec_len = old_rec_len - ideal_len; 
                } else {
                    entry->rec_len = old_rec_len; 
                }

                entry->inode = new_inode_num;
                entry->name_len = request->name_len;
                entry->file_type = request->is_directory ? EXT2_FT_DIR : EXT2_FT_REG_FILE;
                
                memset((char*)entry + sizeof(struct EXT2DirectoryEntry), 0, entry->name_len + 4); 
                memcpy((char*)entry + sizeof(struct EXT2DirectoryEntry), request->name, request->name_len);

                write_blocks(&p_dir_buf, p_lba, 1);
                entry_added = true;
                break; 
            }
            offset += entry->rec_len;
        }
        if (entry_added) break; 
    }

    if (!entry_added) return -1; 

    struct BlockBuffer bgd_buf;
    memcpy(bgd_buf.buf, &bgd_table, sizeof(bgd_table));
    write_blocks(&bgd_buf, 2, 1);
    
    p4_write_inode(request->parent_inode, &parent_inode);

    return 0; 
}