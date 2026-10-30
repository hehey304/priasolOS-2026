#include <stdint.h>
#include <stdbool.h>
#include "header/memory/swap.h"
#include "header/memory/paging.h"
#include "header/filesystem/ext2.h"     /* DISK_SPACE, dan BLOCK_SIZE + read_blocks/write_blocks via disk.h */
#include "header/stdlib/string.h"

/* Didefinisikan di paging.c; kit paging.h belum mendeklarasikannya. Deklarasi ganda yang sama aman. */
extern struct PageManagerState page_manager_state;

/* ================================ konstanta ================================ *
 * Desain swap (Chapter 3, single address space):
 *
 *  - Satu slot swap = satu page frame (4 KiB) = SWAP_BLOCKS_PER_PAGE block disk.
 *  - Lokasi page di swap dicatat pada PTE-nya sendiri. Saat present = 0, CPU mengabaikan
 *    semua bit lain pada PTE, jadi OS bebas memakainya:
 *        bit 31..12 : nomor slot swap
 *        bit  9     : PTE_SWAPPED (penanda "page ini ada di swap", ini bit "ignored" milik OS)
 *        bit  2..1  : flag user / write asli, dipertahankan agar swap in memulihkan permission
 *        bit  0     : present = 0
 *    PTE dengan present = 0 tanpa PTE_SWAPPED berarti page belum pernah dialokasikan.
 *  - Slot dicatat oleh swap_manager (bitmap swap_used[]).
 *  - Page replacement: algoritma clock (second chance) memakai bit Accessed (bit 5) yang
 *    di-set otomatis oleh MMU.
 *  - Semua entry paging diakses sebagai uint32_t mentah supaya tidak bergantung pada nama
 *    field bitfield di paging.h; layout bit mengikuti Intel Manual Vol 3A - 32-bit paging.
 * ========================================================================== */

#define PTE_PRESENT     (1u << 0)
#define PTE_WRITE       (1u << 1)
#define PTE_USER        (1u << 2)
#define PTE_ACCESSED    (1u << 5)
#define PTE_SWAPPED     (1u << 9)
#define PTE_PERM_MASK   (PTE_WRITE | PTE_USER)
#define PTE_FRAME_MASK  0xFFFFF000u

#define PF_PROTECTION   (1u << 0)           /* error code bit 0: 1 = protection violation, 0 = page not present */

#define SWAP_KERNEL_BASE      0xC0000000u   /* higher half kernel */
#define SWAP_USER_PDE_COUNT   (SWAP_KERNEL_BASE >> 22)   /* 768 page directory entry milik user space */
#define SWAP_USER_PAGE_COUNT  (SWAP_USER_PDE_COUNT * PAGE_ENTRY_COUNT)

#define SWAP_BLOCKS_PER_PAGE  (PAGE_FRAME_SIZE / BLOCK_SIZE)

/**
 * Block pertama area swap pada disk.
 * Default: ujung belakang area EXT2 (disk 4 MiB), sehingga storage.bin tidak perlu diubah.
 * EXT2 mengalokasikan block dari LBA kecil dulu, jadi ujung disk baru terpakai jika disk hampir penuh.
 * Jika ingin aman penuh: perbesar storage.bin (mis. 8 MiB) dan definisikan
 *     SWAP_START_LBA (DISK_SPACE / BLOCK_SIZE)
 * agar swap berada di luar area EXT2.
 */
#ifndef SWAP_START_LBA
#define SWAP_START_LBA ((DISK_SPACE / BLOCK_SIZE) - PAGE_FRAME_ON_SWAP_MAX_COUNT * SWAP_BLOCKS_PER_PAGE)
#endif

typedef uint32_t __attribute__((may_alias)) swap_entry_t;

/* ============================ akses hardware (hook) ============================ *
 * Dikumpulkan di satu tempat. Pada build kernel semuanya inline. Tes di host
 * mendefinisikan SWAP_ARCH_OVERRIDE dan menyediakan versi simulasinya sendiri.
 * ========================================================================== */
#ifdef SWAP_ARCH_OVERRIDE

struct PageDirectory *swap_arch_current_dir(void);
bool                  swap_arch_is_active(struct PageDirectory *page_dir);
swap_entry_t         *swap_arch_page_table(uint32_t page_directory_index);
void                 *swap_arch_page_ptr(uint32_t virtual_addr);
void                  swap_arch_flush(uint32_t virtual_addr);
uint32_t              swap_arch_read_cr2(void);
void                  swap_arch_fatal(const char *msg, uint32_t addr, uint32_t error_code);

#else

#include "header/text/framebuffer.h"

static inline uint32_t swap_arch_cr3(void) {
    uint32_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

/* Page directory aktif (virtual address, di higher half kernel) */
static inline struct PageDirectory *swap_arch_current_dir(void) {
    return (struct PageDirectory *) ((swap_arch_cr3() & PTE_FRAME_MASK) + SWAP_KERNEL_BASE);
}

/* true jika page_dir adalah page directory yang sedang dipakai CPU (menerima alamat virtual maupun fisik) */
static inline bool swap_arch_is_active(struct PageDirectory *page_dir) {
    uint32_t addr = (uint32_t) page_dir;
    if (addr >= SWAP_KERNEL_BASE)
        addr -= SWAP_KERNEL_BASE;
    return addr == (swap_arch_cr3() & PTE_FRAME_MASK);
}

/* Page table ke-index lewat recursive mapping (PDE 1023 menunjuk ke page directory itu sendiri) */
static inline swap_entry_t *swap_arch_page_table(uint32_t page_directory_index) {
    return (swap_entry_t *) (RECURSIVE_PAGE_TABLES_VIRTUAL_ADDRESS + page_directory_index * PAGE_FRAME_SIZE);
}

static inline void *swap_arch_page_ptr(uint32_t virtual_addr) {
    return (void *) virtual_addr;
}

static inline void swap_arch_flush(uint32_t virtual_addr) {
    flush_single_tlb((void *) virtual_addr);
}

static inline uint32_t swap_arch_read_cr2(void) {
    return read_cr2();
}

static uint8_t fatal_put_str(uint8_t col, const char *s) {
    for (; *s && col < 79; s++)
        framebuffer_write(24, col++, *s, 0xF, 0x4);
    return col;
}

static uint8_t fatal_put_hex(uint8_t col, uint32_t value) {
    for (int shift = 28; shift >= 0; shift -= 4) {
        uint32_t nibble = (value >> shift) & 0xF;
        framebuffer_write(24, col++, (char) (nibble < 10 ? '0' + nibble : 'A' + nibble - 10), 0xF, 0x4);
    }
    return col;
}

/* Kesalahan yang tidak bisa dipulihkan: tulis pesan di baris terakhir layar, lalu berhenti. */
static void swap_arch_fatal(const char *msg, uint32_t addr, uint32_t error_code) {
    uint8_t col = fatal_put_str(0, msg);
    col = fatal_put_str(col, " addr=0x");
    col = fatal_put_hex(col, addr);
    col = fatal_put_str(col, " err=0x");
    fatal_put_hex(col, error_code);
    __asm__ volatile ("cli");
    for (;;)
        __asm__ volatile ("hlt");
}

#endif /* SWAP_ARCH_OVERRIDE */

/* ============================== swap manager ============================== */

struct SwapManager swap_manager = {
    .swap_used  = {false},
    .free_count = PAGE_FRAME_ON_SWAP_MAX_COUNT,
};

static bool slot_alloc(uint32_t *slot) {
    for (uint32_t i = 0; i < PAGE_FRAME_ON_SWAP_MAX_COUNT; i++) {
        if (!swap_manager.swap_used[i]) {
            swap_manager.swap_used[i] = true;
            swap_manager.free_count--;
            *slot = i;
            return true;
        }
    }
    return false;
}

static void slot_free(uint32_t slot) {
    if (slot < PAGE_FRAME_ON_SWAP_MAX_COUNT && swap_manager.swap_used[slot]) {
        swap_manager.swap_used[slot] = false;
        swap_manager.free_count++;
    }
}

static void slot_write(uint32_t slot, const void *page) {
    write_blocks(page, SWAP_START_LBA + slot * SWAP_BLOCKS_PER_PAGE, SWAP_BLOCKS_PER_PAGE);
}

static void slot_read(uint32_t slot, void *page) {
    read_blocks(page, SWAP_START_LBA + slot * SWAP_BLOCKS_PER_PAGE, SWAP_BLOCKS_PER_PAGE);
}

/* ============================== physical frame ============================== *
 * Memakai page_manager_state milik paging.c: indeks page_frame_map = alamat fisik >> 12.
 * ========================================================================== */

static bool frame_alloc(uint32_t *frame) {
    for (uint32_t f = KERNEL_RESERVED_PAGE_FRAME_COUNT; f < PAGE_FRAME_MAX_COUNT; f++) {
        if (!page_manager_state.page_frame_map[f]) {
            page_manager_state.page_frame_map[f] = true;
            page_manager_state.free_page_frame_count--;
            *frame = f;
            return true;
        }
    }
    return false;
}

static void frame_free(uint32_t frame) {
    if (frame >= KERNEL_RESERVED_PAGE_FRAME_COUNT && frame < PAGE_FRAME_MAX_COUNT &&
        page_manager_state.page_frame_map[frame]) {
        page_manager_state.page_frame_map[frame] = false;
        page_manager_state.free_page_frame_count++;
    }
}

/* ================================ PTE lookup ================================ */

/**
 * Cari PTE untuk virtual address pada page directory AKTIF.
 * Tidak mengalokasikan apa pun (berbeda dengan paging_get_page_table).
 * @return NULL jika page table untuk alamat tersebut belum ada
 */
static swap_entry_t *pte_lookup(struct PageDirectory *page_dir, uint32_t virtual_addr) {
    uint32_t pd_index = virtual_addr >> 22;
    uint32_t pt_index = (virtual_addr >> 12) & 0x3FF;
    swap_entry_t *pde = &((swap_entry_t *) page_dir)[pd_index];
    if (!(*pde & PTE_PRESENT))
        return NULL;
    return &swap_arch_page_table(pd_index)[pt_index];
}

/* ================================ swap out ================================ */

/**
 * Pindahkan satu page dari RAM ke swap.
 * Urutan penting: data ditulis ke disk DULU, baru PTE diubah, TLB di-flush, frame dibebaskan.
 */
static bool swap_out_page(struct PageDirectory *page_dir, uint32_t virtual_addr) {
    uint32_t va = virtual_addr & PTE_FRAME_MASK;
    if (va >= SWAP_KERNEL_BASE || !swap_arch_is_active(page_dir))
        return false;

    swap_entry_t *pte = pte_lookup(page_dir, va);
    if (pte == NULL)
        return false;

    uint32_t entry = *pte;
    if ((entry & (PTE_PRESENT | PTE_USER)) != (PTE_PRESENT | PTE_USER))
        return false;                                   /* hanya page user yang ada di RAM */

    uint32_t slot;
    if (!slot_alloc(&slot))
        return false;                                   /* swap penuh */

    slot_write(slot, swap_arch_page_ptr(va));           /* 1. simpan isi page */
    *pte = (slot << 12) | PTE_SWAPPED | (entry & PTE_PERM_MASK);   /* 2. catat lokasi, present = 0 */
    swap_arch_flush(va);                                /* 3. buang mapping lama dari TLB */
    frame_free(entry >> 12);                            /* 4. bebaskan physical frame */
    return true;
}

void swap_out(struct PageDirectory *page_dir, void *virtual_addr) {
    swap_out_page(page_dir, (uint32_t) (uintptr_t) virtual_addr);
}

/* ============================ page replacement ============================ */

static uint32_t clock_hand = 0;     /* indeks page user (pd_index * 1024 + pt_index) */

/**
 * Clock / second chance pada page user di page directory aktif.
 * Page dengan bit Accessed = 1 diberi kesempatan kedua (bit di-clear), page dengan
 * Accessed = 0 menjadi korban. Dua putaran penuh cukup: putaran pertama membersihkan bit.
 */
static bool pick_victim(struct PageDirectory *page_dir, uint32_t *victim_va) {
    uint32_t scanned = 0;
    while (scanned < 2 * SWAP_USER_PAGE_COUNT) {
        uint32_t pd_index = clock_hand >> 10;
        uint32_t pt_index = clock_hand & 0x3FF;

        if (!(((swap_entry_t *) page_dir)[pd_index] & PTE_PRESENT)) {
            uint32_t skip = PAGE_ENTRY_COUNT - pt_index;        /* lewati satu page table kosong sekaligus */
            clock_hand = (clock_hand + skip) % SWAP_USER_PAGE_COUNT;
            scanned += skip;
            continue;
        }

        swap_entry_t *pte = &swap_arch_page_table(pd_index)[pt_index];
        uint32_t va = (pd_index << 22) | (pt_index << 12);
        clock_hand = (clock_hand + 1) % SWAP_USER_PAGE_COUNT;
        scanned++;

        if ((*pte & (PTE_PRESENT | PTE_USER)) != (PTE_PRESENT | PTE_USER))
            continue;                                           /* kosong, di swap, atau bukan milik user */

        if (*pte & PTE_ACCESSED) {
            *pte &= ~PTE_ACCESSED;
            swap_arch_flush(va);                                /* supaya MMU men-set bit itu lagi saat dipakai */
            continue;
        }

        *victim_va = va;
        return true;
    }
    return false;
}

bool swap_reclaim_frame(void) {
    struct PageDirectory *page_dir = swap_arch_current_dir();
    uint32_t victim;
    if (!pick_victim(page_dir, &victim))
        return false;
    return swap_out_page(page_dir, victim);
}

bool swap_ensure_free_frames(uint32_t count) {
    while (page_manager_state.free_page_frame_count < count) {
        if (!swap_reclaim_frame())
            return false;
    }
    return true;
}

/* ================================= swap in ================================= */

bool swap_in(struct PageDirectory *page_dir, void *virtual_addr) {
    uint32_t va = (uint32_t) (uintptr_t) virtual_addr & PTE_FRAME_MASK;
    if (va >= SWAP_KERNEL_BASE || !swap_arch_is_active(page_dir))
        return false;

    swap_entry_t *pte = pte_lookup(page_dir, va);
    if (pte == NULL || (*pte & PTE_PRESENT) || !(*pte & PTE_SWAPPED))
        return false;                                   /* bukan page yang ada di swap */

    uint32_t slot = *pte >> 12;
    uint32_t perm = *pte & PTE_PERM_MASK;
    if (slot >= PAGE_FRAME_ON_SWAP_MAX_COUNT || !swap_manager.swap_used[slot])
        return false;                                   /* PTE rusak */

    /* 1. Siapkan physical frame (page replacement jika RAM penuh) */
    if (!swap_ensure_free_frames(1))
        return false;
    uint32_t frame;
    if (!frame_alloc(&frame))
        return false;

    /* 2. Pasang mapping dulu, baru isi frame lewat virtual address-nya.
     *    PTE_ACCESSED di-set agar page yang baru masuk tidak langsung jadi korban berikutnya. */
    *pte = (frame << 12) | PTE_ACCESSED | perm | PTE_PRESENT;
    swap_arch_flush(va);

    /* 3. Baca isi page dari swap, lalu lepaskan slot-nya */
    slot_read(slot, swap_arch_page_ptr(va));
    slot_free(slot);
    return true;
}

bool swap_release_if_swapped(struct PageDirectory *page_dir, void *virtual_addr) {
    uint32_t va = (uint32_t) (uintptr_t) virtual_addr & PTE_FRAME_MASK;
    if (va >= SWAP_KERNEL_BASE || !swap_arch_is_active(page_dir))
        return false;

    swap_entry_t *pte = pte_lookup(page_dir, va);
    if (pte == NULL || (*pte & PTE_PRESENT) || !(*pte & PTE_SWAPPED))
        return false;

    slot_free(*pte >> 12);
    *pte = 0;
    swap_arch_flush(va);
    return true;
}

/* ============================== page fault handler ============================== */

void handle_page_fault(uint32_t error_code) {
    uint32_t fault_addr = swap_arch_read_cr2();
    struct PageDirectory *page_dir = swap_arch_current_dir();

    /* Page ada tetapi aksesnya ditolak (mis. user menulis page kernel / read-only): bukan urusan swap */
    if (error_code & PF_PROTECTION) {
        swap_arch_fatal("PAGE FAULT: protection violation", fault_addr, error_code);
        return;
    }

    /* Kernel tidak pernah di-swap, page fault di higher half selalu bug */
    if (fault_addr >= SWAP_KERNEL_BASE) {
        swap_arch_fatal("PAGE FAULT: kernel address", fault_addr, error_code);
        return;
    }

    swap_entry_t *pte = pte_lookup(page_dir, fault_addr);

    /* Kasus 1: page ada di swap -> swap in (termasuk page replacement jika RAM penuh) */
    if (pte != NULL && !(*pte & PTE_PRESENT) && (*pte & PTE_SWAPPED)) {
        if (!swap_in(page_dir, (void *) (uintptr_t) fault_addr))
            swap_arch_fatal("PAGE FAULT: out of memory", fault_addr, error_code);
        return;                     /* iret mengulang instruksi yang tadi gagal */
    }

    /* Kasus 2: belum pernah dialokasikan */
#if SWAP_DEMAND_ALLOCATE
    {
        uint32_t va = fault_addr & PTE_FRAME_MASK;
        if (swap_ensure_free_frames(2) &&
            paging_allocate_user_page_frame(page_dir, (void *) (uintptr_t) va)) {
            memset(swap_arch_page_ptr(va), 0, PAGE_FRAME_SIZE);
            return;
        }
    }
#endif
    swap_arch_fatal("PAGE FAULT: invalid access", fault_addr, error_code);
}

/* ================================== self test ================================== */
#ifdef SWAP_SELFTEST

#define SWAP_TEST_BASE 0x10000000u

static uint8_t test_pattern(uint32_t page, uint32_t offset) {
    return (uint8_t) (page * 131u + offset * 7u + 1u);
}

bool swap_selftest(uint32_t page_count) {
    struct PageDirectory *page_dir = swap_arch_current_dir();
    uint32_t allocated = 0;

    for (uint32_t i = 0; i < page_count; i++) {
        uint32_t va = SWAP_TEST_BASE + i * PAGE_FRAME_SIZE;
        if (!paging_allocate_user_page_frame(page_dir, (void *) (uintptr_t) va))
            break;
        volatile uint8_t *p = (volatile uint8_t *) (uintptr_t) va;
        for (uint32_t j = 0; j < PAGE_FRAME_SIZE; j++)
            p[j] = test_pattern(i, j);
        allocated++;
    }

    bool ok = (allocated == page_count);
    for (uint32_t i = allocated; i > 0 && ok; i--) {            /* baca dari yang terbaru: memaksa swap in + replacement */
        volatile uint8_t *p = (volatile uint8_t *) (uintptr_t) (SWAP_TEST_BASE + (i - 1) * PAGE_FRAME_SIZE);
        for (uint32_t j = 0; j < PAGE_FRAME_SIZE; j++) {
            if (p[j] != test_pattern(i - 1, j)) {
                ok = false;
                break;
            }
        }
    }

    for (uint32_t i = 0; i < allocated; i++)
        paging_free_user_page_frame(page_dir, (void *) (uintptr_t) (SWAP_TEST_BASE + i * PAGE_FRAME_SIZE));
    return ok;
}

#endif