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

static struct EXT2Superblock superblock;
static struct EXT2BlockGroupDescriptorTable bgd_table;

#define EXT2_ROOT_INODE 1

#define DIR_MAX_BLOCKS 12
#define DIR_MAX_BYTES  (DIR_MAX_BLOCKS * BLOCK_SIZE)

uint32_t inode_to_bgd(uint32_t inode) {
    return (inode - 1) / INODES_PER_GROUP;
}

uint32_t inode_to_local(uint32_t inode) {
    return (inode - 1) % INODES_PER_GROUP;
}

char *get_entry_name(void *entry) {
    return (char *)entry + sizeof(struct EXT2DirectoryEntry);
}

struct EXT2DirectoryEntry *get_directory_entry(void *ptr, uint32_t offset) {
    return (struct EXT2DirectoryEntry *)((uint8_t *)ptr + offset);
}

static void inode_location(uint32_t inode, uint32_t *block, uint32_t *offset) {
    uint32_t bgd   = inode_to_bgd(inode);
    uint32_t local = inode_to_local(inode);
    *block  = bgd_table.table[bgd].bg_inode_table + local / INODES_PER_TABLE;
    *offset = (local % INODES_PER_TABLE) * INODE_SIZE;
}

static void read_node(struct EXT2Inode *node, uint32_t inode) {
    uint32_t block, offset;
    struct BlockBuffer b;
    inode_location(inode, &block, &offset);
    read_blocks(&b, block, 1);
    memcpy(node, b.buf + offset, INODE_SIZE);
}

void sync_node(struct EXT2Inode *node, uint32_t inode) {
    uint32_t block, offset;
    struct BlockBuffer b;
    inode_location(inode, &block, &offset);
    read_blocks(&b, block, 1);
    memcpy(b.buf + offset, node, INODE_SIZE);
    write_blocks(&b, block, 1);
}

static bool is_dir_mode(uint16_t mode) {
    return (mode & EXT2_S_IFDIR) || mode == EXT2_FT_DIR;
}

static bool bitmap_get(uint32_t bitmap_block, uint32_t idx) {
    struct BlockBuffer b;
    read_blocks(&b, bitmap_block, 1);
    return (b.buf[idx / 8] >> (idx % 8)) & 1u;
}

static void bitmap_clear(uint32_t bitmap_block, uint32_t idx) {
    struct BlockBuffer b;
    read_blocks(&b, bitmap_block, 1);
    b.buf[idx / 8] &= (uint8_t)~(1u << (idx % 8));
    write_blocks(&b, bitmap_block, 1);   
}

static void free_block(uint32_t block) {
    uint32_t bgd = block / BLOCKS_PER_GROUP;
    uint32_t idx = block % BLOCKS_PER_GROUP;
    if (bgd >= GROUPS_COUNT) return;
    if (!bitmap_get(bgd_table.table[bgd].bg_block_bitmap, idx)) return; /* sudah kosong */

    bitmap_clear(bgd_table.table[bgd].bg_block_bitmap, idx);
    bgd_table.table[bgd].bg_free_blocks_count++;
    superblock.s_free_blocks_count++;
}

static void free_block_tree(uint32_t block, uint32_t depth) {
    if (block == 0) return;
    if (depth > 0) {
        uint32_t ptrs[BLOCK_SIZE / sizeof(uint32_t)];
        read_blocks(ptrs, block, 1);
        for (uint32_t i = 0; i < BLOCK_SIZE / sizeof(uint32_t); i++)
            free_block_tree(ptrs[i], depth - 1);
    }
    free_block(block);
}

static void sync_fs_metadata(void) {
    struct BlockBuffer b;

    memset(&b, 0, sizeof(b));
    memcpy(b.buf, &superblock, sizeof(superblock));
    write_blocks(&b, 1, 1);

    memset(&b, 0, sizeof(b));
    memcpy(b.buf, &bgd_table, sizeof(bgd_table));
    write_blocks(&b, 2, 1);
}

static uint32_t read_dir_table(struct EXT2Inode *dir, uint8_t *buf) {
    uint32_t n = 0;
    while (n < DIR_MAX_BLOCKS && dir->i_block[n] != 0) {
        read_blocks(buf + n * BLOCK_SIZE, dir->i_block[n], 1);
        n++;
    }
    return n * BLOCK_SIZE;
}

static void write_dir_table(struct EXT2Inode *dir, uint8_t *buf, uint32_t size) {
    for (uint32_t n = 0; n * BLOCK_SIZE < size; n++)
        write_blocks(buf + n * BLOCK_SIZE, dir->i_block[n], 1);
}

static bool is_dot_name(const char *name, uint32_t len) {
    return (len == 1 && name[0] == '.') ||
           (len == 2 && name[0] == '.' && name[1] == '.');
}

static int32_t find_entry(uint8_t *table, uint32_t size,
                          const char *name, uint8_t name_len, uint8_t file_type) {
    uint32_t off = 0;
    while (off + sizeof(struct EXT2DirectoryEntry) <= size) {
        struct EXT2DirectoryEntry *e = get_directory_entry(table, off);
        if (e->inode == 0 || e->rec_len == 0) break;
        if (e->name_len == name_len &&
            e->file_type == file_type &&
            memcmp(get_entry_name(e), name, name_len) == 0)
            return (int32_t)off;
        off += e->rec_len;
    }
    return -1;
}

static void remove_entry(uint8_t *table, uint32_t size, uint32_t off) {
    uint32_t len = get_directory_entry(table, off)->rec_len;
    if (len == 0 || off + len > size) return;
    for (uint32_t i = off; i + len < size; i++)
        table[i] = table[i + len];
    memset(table + size - len, 0, len);
}

bool is_directory_empty(uint32_t inode) {
    struct EXT2Inode dir;
    uint8_t table[DIR_MAX_BYTES];
    read_node(&dir, inode);
    uint32_t size = read_dir_table(&dir, table);

    uint32_t off = 0;
    while (off + sizeof(struct EXT2DirectoryEntry) <= size) {
        struct EXT2DirectoryEntry *e = get_directory_entry(table, off);
        if (e->inode == 0 || e->rec_len == 0) break;
        if (!is_dot_name(get_entry_name(e), e->name_len))
            return false;              
        off += e->rec_len;
    }
    return true;
}

void deallocate_node(uint32_t inode) {
    struct EXT2Inode node;
    read_node(&node, inode);

    for (uint32_t i = 0; i < 12; i++)
        free_block_tree(node.i_block[i], 0);
    free_block_tree(node.i_block[12], 1);
    free_block_tree(node.i_block[13], 2);
    free_block_tree(node.i_block[14], 3);

    bool was_dir = is_dir_mode(node.i_mode);
    memset(&node, 0, sizeof(node));
    sync_node(&node, inode);

    uint32_t bgd = inode_to_bgd(inode);
    bitmap_clear(bgd_table.table[bgd].bg_inode_bitmap, inode_to_local(inode));
    bgd_table.table[bgd].bg_free_inodes_count++;
    if (was_dir && bgd_table.table[bgd].bg_used_dirs_count > 0)
        bgd_table.table[bgd].bg_used_dirs_count--;
    superblock.s_free_inodes_count++;
}

int8_t delete(struct EXT2DriverRequest request) {
    uint32_t max_inode = GROUPS_COUNT * INODES_PER_GROUP;
    uint32_t parent_ino = request.parent_inode;

    if (parent_ino == 0 || parent_ino > max_inode)
        return 3;
    if (!bitmap_get(bgd_table.table[inode_to_bgd(parent_ino)].bg_inode_bitmap,
                    inode_to_local(parent_ino)))
        return 3;                                  

    struct EXT2Inode parent;
    read_node(&parent, parent_ino);
    if (!is_dir_mode(parent.i_mode))
        return 3;                                  

    uint8_t parent_table[DIR_MAX_BYTES];
    uint32_t parent_size = read_dir_table(&parent, parent_table);
    if (parent_size == 0)
        return 3;                      
    if (request.name == NULL || request.name_len == 0)
        return -1;
    if (is_dot_name(request.name, request.name_len))
        return -1;

    uint8_t type = request.is_directory ? EXT2_FT_DIR : EXT2_FT_REG_FILE;
    int32_t off = find_entry(parent_table, parent_size,
                             request.name, request.name_len, type);
    if (off < 0)
        return 1;

    uint32_t target = get_directory_entry(parent_table, (uint32_t)off)->inode;
    if (target == 0 || target > max_inode)
        return -1;
    if (target == EXT2_ROOT_INODE || target == parent_ino)
        return -1;                                
    if (request.is_directory && !is_directory_empty(target))
        return 2;

    deallocate_node(target);                                  
    remove_entry(parent_table, parent_size, (uint32_t)off);   
    write_dir_table(&parent, parent_table, parent_size);      
    sync_fs_metadata();                                       

    return 0;
}