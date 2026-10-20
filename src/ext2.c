#include <stdint.h>
#include <stdbool.h>
#include "header/stdlib/string.h"
#include "header/filesystem/ext2.h"
#include "header/text/framebuffer.h"

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
static struct EXT2Superblock sblock;

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

bool is_empty_storage(void) {
    struct BlockBuffer b;
    
    // baca satu block dari disk
    read_blocks(&b, BOOT_SECTOR, 1);
    
    // bandingin blok yang dibaca dengan fs_signature, harus sama persis agar disk bisa dibaca
    return memcmp(b.buf, fs_signature, BLOCK_SIZE) != 0;
}

void create_ext2(void) {
    //Write signature to the Boot Sector
    write_blocks((void *)fs_signature, BOOT_SECTOR, 1);


    // Fill in global stats using the constants defined in ext2.h
    sblock.s_inodes_count = INODES_PER_GROUP * GROUPS_COUNT;
    sblock.s_blocks_count = DISK_SPACE / BLOCK_SIZE;
    
    // Set the magic signature so the OS knows this is EXT2
    sblock.s_magic = EXT2_SUPER_MAGIC;
    
    // Set the group metrics
    sblock.s_blocks_per_group = BLOCKS_PER_GROUP;
    sblock.s_inodes_per_group = INODES_PER_GROUP;
    
    // By convention, Block 0 is Boot Sector, Block 1 is the Superblock.
    sblock.s_first_data_block = 1; 

    struct BlockBuffer sb_buf = {0};
    memcpy(sb_buf.buf, &sblock, sizeof(sblock));
    write_blocks(&sb_buf, 1, 1);
    
    // Build the Block Group Descriptor Table (BGDT) ---
    // ext2.c has a global variable called 'bgd_table'.
    // Blocks 0 (Boot), 1 (Superblock), and 2 (BGDT) are taken.
    // The first available data block for our bitmaps and tables is Block 3.
    uint32_t current_lba = 3; 

    // Loop through all 8 groups (GROUPS_COUNT) to assign their addresses
    for (uint32_t i = 0; i < GROUPS_COUNT; i++) {
        // 1. Assign address for the Block Bitmap (needs 1 block)
        bgd_table.table[i].bg_block_bitmap = current_lba;
        current_lba += 1; // Move the address counter forward by 1
        
        // 2. Assign address for the Inode Bitmap (needs 1 block)
        bgd_table.table[i].bg_inode_bitmap = current_lba;
        current_lba += 1; // Move the address counter forward by 1
        
        // 3. Assign address for the Inode Table 
        // (INODES_TABLE_BLOCK_COUNT is defined as 16 blocks)
        bgd_table.table[i].bg_inode_table = current_lba;
        current_lba += INODES_TABLE_BLOCK_COUNT; // Move counter forward by 16
        
        // 4. Set the initial "free" counts for this group
        bgd_table.table[i].bg_free_inodes_count = INODES_PER_GROUP;
        bgd_table.table[i].bg_free_blocks_count = BLOCKS_PER_GROUP;
        bgd_table.table[i].bg_used_dirs_count = 0;
    }


    // --- STEP 4: Build the blank "buildings" (Bitmaps & Tables) ---
    // Create one 512-byte buffer in RAM and fill it entirely with zeroes
    struct BlockBuffer empty_block = {0}; 

    // Loop through all 8 groups to physically write the empty data to the disk
    for (uint32_t i = 0; i < GROUPS_COUNT; i++) {
        // 1. Stamp 1 block of zeroes at the Block Bitmap address
        write_blocks(&empty_block, bgd_table.table[i].bg_block_bitmap, 1);
        
        // 2. Stamp 1 block of zeroes at the Inode Bitmap address
        write_blocks(&empty_block, bgd_table.table[i].bg_inode_bitmap, 1);
        
        // 3. Stamp 16 blocks of zeroes starting at the Inode Table address
        // (INODES_TABLE_BLOCK_COUNT is defined as 16 in ext2.h)
        for (uint32_t j = 0; j < INODES_TABLE_BLOCK_COUNT; j++) {
            write_blocks(&empty_block, bgd_table.table[i].bg_inode_table + j, 1);
        }
    }

    struct BlockBuffer inode_bitmap_buf;
    read_blocks(&inode_bitmap_buf, bgd_table.table[0].bg_inode_bitmap, 1);
    
    // Set the first bit (bit 0 corresponds to Inode #1) to '1'
    inode_bitmap_buf.buf[0] |= (1 << 0);
    
    // Write the updated Inode Bitmap back to disk
    write_blocks(&inode_bitmap_buf, bgd_table.table[0].bg_inode_bitmap, 1);

    // 2. Set up the Root Directory's Inode (Inode #1)
    struct EXT2Inode root_inode = {0};
    root_inode.i_mode = EXT2_FT_DIR; // Mark it as a directory[cite: 1]
    root_inode.i_size = BLOCK_SIZE;   // Size is one full block
    root_inode.i_blocks = 1;          // Consumes 1 data block
    
    // Pick the next available block for the root directory's data contents
    // (current_lba is currently pointing right after the inode tables)
    uint32_t root_data_lba = current_lba;
    root_inode.i_block[0] = root_data_lba;

    // Write this root inode into Group 0's Inode Table at index 0 (which is Inode #1)
    struct BlockBuffer inode_table_buf;
    read_blocks(&inode_table_buf, bgd_table.table[0].bg_inode_table, 1);
    
    // Copy our root_inode struct into the first slot of the table buffer
    memcpy(inode_table_buf.buf, &root_inode, sizeof(struct EXT2Inode));
    write_blocks(&inode_table_buf, bgd_table.table[0].bg_inode_table, 1);

    // 3. Write the mandatory "." and ".." directory entries into the root data block
    struct BlockBuffer root_data_buf = {0};
    
    // Create the "." entry (points to itself: Inode #1)
    struct EXT2DirectoryEntry *dot = (struct EXT2DirectoryEntry *) root_data_buf.buf;
    dot->inode = 1;
    dot->file_type = EXT2_FT_DIR; //[cite: 1]
    dot->name_len = 1;
    dot->rec_len = 12; 
    get_entry_name(dot)[0] = '.'; // name string right after struct

    // Create the ".." entry (root's parent is also itself: Inode #1)
    // It starts right after the "." entry
    struct EXT2DirectoryEntry *dotdot = (struct EXT2DirectoryEntry *) (root_data_buf.buf + dot->rec_len);
    dotdot->inode = 1;
    dotdot->file_type = EXT2_FT_DIR; //[cite: 1]
    dotdot->name_len = 2;
    // In EXT2, the last entry's rec_len must stretch all the way to the end of the block
    dotdot->rec_len = BLOCK_SIZE - dot->rec_len; 
    get_entry_name(dotdot)[0] = '.';
    get_entry_name(dotdot)[1] = '.';

    // Write this data block out to the disk
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

/* ============================ delete() ============================ */
int8_t delete(struct EXT2DriverRequest *request) {
    return ext2_dir_remove(request->parent_inode, request->name, request->name_len);
}