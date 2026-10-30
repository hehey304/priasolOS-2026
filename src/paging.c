#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/memory/paging.h"
#include "header/memory/swap.h"
#include "header/stdlib/string.h"

/* =================================================================================
 * ORANG 1 - 3.1 Paging & Memory Manager
 * ================================================================================= */

/**
 * _paging_kernel_page_directory and _paging_kernel_page_table is initialized on kernel-entrypoint.s
 */
__attribute__((aligned(0x1000))) struct PageDirectory _paging_kernel_page_directory;

__attribute__((aligned(0x1000))) struct PageTable _paging_kernel_page_table;

/**
 * Page table untuk user space. Disimpan di memori kernel (higher half) sehingga alamat
 * fisiknya cukup dihitung dengan "virtual - KERNEL_VIRTUAL_ADDRESS_BASE" dan tidak
 * memakan page frame user. Ini pengganti pendekatan recursive mapping pada kit.
 */
__attribute__((aligned(0x1000))) static struct PageTable user_page_table_list[PAGING_USER_PAGE_TABLE_COUNT];
static bool user_page_table_used[PAGING_USER_PAGE_TABLE_COUNT];

/**
 * Global page frame manager state tracking free/busy frames.
 * Frame milik kernel (4 MiB pertama) dan frame "Reserved" di ujung RAM ditandai terpakai.
 */
struct PageManagerState page_manager_state = {
    .page_frame_map = {
        [0 ... KERNEL_RESERVED_PAGE_FRAME_COUNT - 1]          = true,
        [PAGE_FRAME_USABLE_COUNT ... PAGE_FRAME_MAX_COUNT - 1] = true,
    },
    .free_page_frame_count = PAGE_FRAME_USABLE_COUNT - KERNEL_RESERVED_PAGE_FRAME_COUNT,
};

struct PageDirectory* paging_get_current_page_directory_addr(void) {
    uint32_t current_page_directory_phys_addr;
    __asm__ volatile("mov %%cr3, %0" : "=r"(current_page_directory_phys_addr) : /* <Empty> */);
    return (struct PageDirectory*) (current_page_directory_phys_addr + KERNEL_VIRTUAL_ADDRESS_BASE);
}

struct PageTable *paging_get_page_table(struct PageDirectory *page_dir, uint32_t page_directory_index) {
    if (page_directory_index >= PAGE_ENTRY_COUNT)
        return NULL;

    struct PageDirectoryEntry *dir_entry = &page_dir->table[page_directory_index];
    if (dir_entry->flag.present_bit)
        return (struct PageTable *) (((uint32_t) dir_entry->page_table_base_address << 12) + KERNEL_VIRTUAL_ADDRESS_BASE);

    // Page table baru hanya dibuat untuk user space, area kernel sudah disiapkan kernel-entrypoint.s
    if (page_directory_index >= (KERNEL_VIRTUAL_ADDRESS_BASE >> 22))
        return NULL;

    for (uint32_t i = 0; i < PAGING_USER_PAGE_TABLE_COUNT; i++) {
        if (!user_page_table_used[i]) {
            user_page_table_used[i] = true;
            memset(&user_page_table_list[i], 0, sizeof(struct PageTable));

            dir_entry->flag = (struct PageDirectoryEntryFlag) {
                .present_bit         = 1,
                .write_bit           = 1,
                .user_supervisor_bit = 1,
            };
            dir_entry->global_page             = 0;
            dir_entry->ignored                 = 0;
            dir_entry->page_table_base_address = ((uint32_t) &user_page_table_list[i] - KERNEL_VIRTUAL_ADDRESS_BASE) >> 12;
            return &user_page_table_list[i];
        }
    }
    return NULL;
}

void update_page_directory_entry(
    struct PageDirectory *page_dir,
    void *physical_addr,
    void *virtual_addr,
    struct PageTableEntryFlag flag
) {
    uint32_t page_directory_index = ((uint32_t) virtual_addr >> 22) & 0x3FF;
    uint32_t page_table_index     = ((uint32_t) virtual_addr >> 12) & 0x3FF;
    struct PageTable *page_table  = paging_get_page_table(page_dir, page_directory_index);
    if (page_table == NULL)
        return;

    struct PageTableEntry *entry      = &page_table->table[page_table_index];
    entry->flag                       = flag;
    entry->global_page                = 0;
    entry->ignored                    = 0;
    entry->physical_page_base_address = (uint32_t) physical_addr >> 12;
    flush_single_tlb(virtual_addr);
}

void flush_single_tlb(void *virtual_addr) {
    __asm__ volatile("invlpg (%0)" : /* <Empty> */ : "b"(virtual_addr) : "memory");
}



/* --- Memory Management --- */
bool paging_allocate_check(uint32_t amount) {
    // Frame yang habis masih bisa didapat dengan memindahkan page lain ke swap space
    uint32_t required_frames = (amount + PAGE_FRAME_SIZE - 1) / PAGE_FRAME_SIZE;
    return required_frames <= page_manager_state.free_page_frame_count + swap_manager.free_count;
}


bool paging_allocate_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr) {
    uint32_t virtual_page = (uint32_t) virtual_addr & ~(PAGE_FRAME_SIZE - 1);
    if (virtual_page >= KERNEL_VIRTUAL_ADDRESS_BASE)
        return false;   // kernel space tidak boleh disentuh

    // Siapkan page table terlebih dahulu agar frame tidak terbuang jika page table habis
    struct PageTable *page_table = paging_get_page_table(page_dir, virtual_page >> 22);
    if (page_table == NULL)
        return false;
    if (page_table->table[(virtual_page >> 12) & 0x3FF].flag.present_bit)
        return true;    // sudah terpetakan

    // First fit: frame kosong dengan nomor terkecil
    for (uint32_t frame = KERNEL_RESERVED_PAGE_FRAME_COUNT; frame < PAGE_FRAME_USABLE_COUNT; frame++) {
        if (page_manager_state.page_frame_map[frame])
            continue;

        page_manager_state.page_frame_map[frame] = true;
        page_manager_state.free_page_frame_count--;
        update_page_directory_entry(
            page_dir,
            (void *) (frame * PAGE_FRAME_SIZE),
            (void *) virtual_page,
            (struct PageTableEntryFlag) {
                .present_bit         = 1,
                .write_bit           = 1,
                .user_supervisor_bit = 1,
            }
        );

        // Bersihkan isi frame agar data milik pemakai sebelumnya tidak terbaca
        if (page_dir == paging_get_current_page_directory_addr())
            memset((void *) virtual_page, 0, PAGE_FRAME_SIZE);
        return true;
    }
    return false;
}

bool paging_free_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr) {
    uint32_t virtual_page = (uint32_t) virtual_addr & ~(PAGE_FRAME_SIZE - 1);
    if (virtual_page >= KERNEL_VIRTUAL_ADDRESS_BASE || !page_dir->table[virtual_page >> 22].flag.present_bit)
        return false;

    struct PageTable *page_table = paging_get_page_table(page_dir, virtual_page >> 22);
    struct PageTableEntry *entry = &page_table->table[(virtual_page >> 12) & 0x3FF];
    if (!entry->flag.present_bit)
        return false;

    uint32_t frame = entry->physical_page_base_address;
    if (frame < KERNEL_RESERVED_PAGE_FRAME_COUNT || frame >= PAGE_FRAME_USABLE_COUNT)
        return false;   // frame kernel tidak boleh dibebaskan

    page_manager_state.page_frame_map[frame] = false;
    page_manager_state.free_page_frame_count++;
    memset(entry, 0, sizeof(struct PageTableEntry));
    flush_single_tlb((void *) virtual_page);
    return true;
}
