#ifndef _PAGING_H
#define _PAGING_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
/**
 * Note: MB often referring to MiB in context of memory management
 * change this to be a small number to test if swap working properly
 * kernel will be tested with a small number of SYSTEM_MEMORY_MB
 */
 #define SYSTEM_MEMORY_MB     128

// Number of entries in a page directory
#define PAGE_ENTRY_COUNT     1024

// Page Frame (PF) Size: 4 KiB
#define PAGE_FRAME_SIZE      0x1000u

// Higher half kernel: physical 0x0 - 0x400000 is mapped into virtual 0xC0000000 - 0xC0400000
#define KERNEL_VIRTUAL_ADDRESS_BASE 0xC0000000

#define KERNEL_RESERVED_PAGE_FRAME_COUNT (0x400000u / PAGE_FRAME_SIZE)

#define RECURSIVE_PAGE_TABLES_VIRTUAL_ADDRESS 0xFFC00000u
#define RECURSIVE_PAGE_DIRECTORY_INDEX 1023u

// Maximum usable page frame. Default count: 128 MiB / 4 KiB = 32768 frames
#define PAGE_FRAME_MAX_COUNT ((SYSTEM_MEMORY_MB << 20) / PAGE_FRAME_SIZE)

// Physical memory hole on top of RAM (QEMU 128 MiB: 0x7FE0000 - 0x8000000 is "Reserved")
#define PAGE_FRAME_RESERVED_TOP_COUNT (0x20000u / PAGE_FRAME_SIZE)
#define PAGE_FRAME_USABLE_COUNT (PAGE_FRAME_MAX_COUNT - PAGE_FRAME_RESERVED_TOP_COUNT)

// Page table for user space (virtual 0x0 - 0xC0000000) is taken from this pool inside kernel memory
#define PAGE_TABLE_POOL_COUNT 64

// First frame index that is NOT usable (frames at/above this are the reserved hole)

// kernel's  page directory, using 4 KiB pages
extern struct PageDirectory _paging_kernel_page_directory;
extern struct PageTable _paging_kernel_page_table;

/**
 * Page Directory Entry Flag, only first 8 bit
 * @param present_bit        Indicate whether this entry is exist or not.
 * @param write_bit          Indicate whether this page is writable
 * @param us_bit             User / Supervisor, true if user mode (ring 3) can access this entry
 * @param write_through_bit  Page-level write-through (PWT)
 * @param cache_disable_bit  Page-level cache disable (PCD)
 * @param accessed_bit       Set by MMU when this entry is used for translation
 * @param ignored_bit        Ignored for 4 KiB page
 * @param use_pagesize_4_mb Indicate whether this entry use 4 MB page size (set false)
 * References: Intel Manual 3a - Ch 4 Paging - Figure 4-4 PDE: 4KB page
 */
struct PageDirectoryEntryFlag {
    uint32_t present_bit        : 1;
    uint32_t write_bit          : 1;
    uint32_t us_bit             : 1;
    uint32_t write_through_bit  : 1;
    uint32_t cache_disable_bit  : 1;
    uint32_t accessed_bit       : 1;
    uint32_t ignored_bit        : 1;
    uint32_t use_pagesize_4_mb  : 1;
} __attribute__((packed));

/**
 * Page Table Entry Flag, only first 8 bit
 * @param present_bit        Indicate whether this entry is exist or not.
 * @param write_bit          Indicate whether this page is writable
 * @param us_bit             User / Supervisor, true if user mode (ring 3) can access this page
 * @param write_through_bit  Page-level write-through (PWT)
 * @param cache_disable_bit  Page-level cache disable (PCD)
 * @param accessed_bit       Set by MMU when this page is accessed
 * @param dirty_bit          Set by MMU when this page is written
 * @param page_attr_table    Page Attribute Table bit (PAT)
 * References: Intel Manual 3a - Ch 4 Paging - Figure 4-4 PTE: 4KB page
 */
struct PageTableEntryFlag {
    uint32_t present_bit        : 1;
    uint32_t write_bit          : 1;
    uint32_t us_bit             : 1;
    uint32_t write_through_bit  : 1;
    uint32_t cache_disable_bit  : 1;
    uint32_t accessed_bit       : 1;
    uint32_t dirty_bit          : 1;
    uint32_t page_attr_table    : 1;
} __attribute__((packed));

/**
 * Page Directory Entry, for page size 4 KB.
 * References: Intel Manual 3a - Ch 4 Paging - Figure 4-4 PDE: 4KB page
 *
 * @param flag                    Contain 8-bit page directory entry flag
 * @param global_page             Bit 8, ignored for PDE that points into page table (set 0)
 * @param ignored                 Ignored bit (3-bit)
 * @param page_table_base_address Bits 31:12 of page table physical address (20-bit)
 */
struct PageDirectoryEntry {
    struct PageDirectoryEntryFlag flag;
    uint16_t global_page             : 1;
    uint16_t ignored                 : 3;
    uint32_t page_table_base_address : 20;
}__attribute__((packed));

/**
 * Page Table Entry, for page size 4 KB.
 * References: Intel Manual 3a - Ch 4 Paging - Figure 4-4 PTE: 4KB page
 *
 * @param flag                       Contain 8-bit page table entry flag
 * @param global_page                G flag
 * @param ignored                    Ignored bit (3-bit)
 * @param physical_page_base_address Bits 31:12 of page frame physical address (20-bit), act as frame index
 */
struct PageTableEntry {
    struct PageTableEntryFlag flag;
    uint16_t global_page                : 1;
    uint16_t ignored                    : 3;
    uint32_t physical_page_base_address : 20;
}__attribute__((packed));

/**
 * Page Directory, contain array of PageDirectoryEntry.
 * Note: This data structure is volatile (can be modified from outside this code, check "C volatile keyword").
 * MMU operation, TLB hit & miss also affecting this data structure (dirty, accessed bit, etc).
 *
 * Warning: Address must be aligned in 4 KB (listed on Intel Manual), use __attribute__((aligned(0x1000))),
 * unaligned definition of PageDirectory will cause triple fault
 *
 * @param table Fixed-width array of PageDirectoryEntry with size PAGE_ENTRY_COUNT
 */
struct PageDirectory {
    struct PageDirectoryEntry table[PAGE_ENTRY_COUNT];
}__attribute__((packed));

/**
 *  Page Table, contain array of PageTableEntry
 */
struct PageTable {
    struct PageTableEntry table[PAGE_ENTRY_COUNT];
}__attribute__((packed));

/**
 * Containing page manager states.
 *
 * @param page_frame_map        Keeping track empty space. True when the page frame is currently used
 * @param free_page_frame_count Number of page frame that still free
 * @param page_table_used       True when page table in _paging_user_page_table_pool is currently used
 */
struct PageManagerState {
    bool     page_frame_map[PAGE_FRAME_MAX_COUNT];
    uint32_t free_page_frame_count;
    bool     page_table_used[PAGE_TABLE_POOL_COUNT];
} __attribute__((packed));

extern struct PageManagerState page_manager_state;





/* --- Memory Management --- */
/**
 * Check whether a certain amount of physical memory is available
 *
 * @param amount Requested amount of physical memory in bytes
 * @return       Return true when there's enough free memory available
 */
bool paging_allocate_check(uint32_t amount);

/**
 * Allocate single user page frame in page directory
 *
 * @param page_dir     Page directory to update
 * @param virtual_addr Virtual address to be allocated
 * @return             Will return true if success, false otherwise
 */
bool paging_allocate_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr);

/**
 * Deallocate single user page frame in page directory
 *
 * @param page_dir      Page directory to update
 * @param virtual_addr  Virtual address to be deallocated
 * @return              Will return true if success, false otherwise
 */
bool paging_free_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr);

void flush_single_tlb(void *virtual_addr);
struct PageTable *paging_get_page_table(struct PageDirectory *page_dir, uint32_t page_directory_index);
void update_page_directory_entry(
    struct PageDirectory *page_dir,
    void *physical_addr,
    void *virtual_addr,
    struct PageTableEntryFlag flag
);

#endif
