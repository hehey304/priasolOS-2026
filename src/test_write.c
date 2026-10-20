#include <stdio.h>
#include <string.h>
#include "header/filesystem/ext2.h"

// =========================================================================
// MOCK DISK DRIVER (Fungsi Tiruan agar bisa running di terminal WSL/Linux)
// =========================================================================
void read_blocks(void *ptr, uint32_t logical_block_address, uint8_t block_count) {
    FILE *f = fopen("bin/storage.bin", "rb");
    if (f) {
        fseek(f, logical_block_address * BLOCK_SIZE, SEEK_SET);
        fread(ptr, BLOCK_SIZE, block_count, f);
        fclose(f);
    } else {
        printf("Error: Gagal membuka bin/storage.bin untuk dibaca.\n");
    }
}

void write_blocks(const void *ptr, uint32_t logical_block_address, uint8_t block_count) {
    FILE *f = fopen("bin/storage.bin", "rb+");
    if (f) {
        fseek(f, logical_block_address * BLOCK_SIZE, SEEK_SET);
        fwrite(ptr, BLOCK_SIZE, block_count, f);
        fclose(f);
    } else {
        printf("Error: Gagal membuka bin/storage.bin untuk ditulis.\n");
    }
}
// =========================================================================

int main() {
    printf("=== Mulai Test Write EXT2 (Tugas P4) ===\n");

    // Wajib: Inisialisasi OS sebelum nulis
    initialize_filesystem_ext2();

    // ==========================================
    // TEST 1: Membuat folder 'folder_andro'
    // ==========================================
    printf("Test 1: Membuat folder 'folder_andro'...\n");
    struct EXT2DriverRequest req1 = {0};
    req1.parent_inode = 1; // Root directory
    req1.name = "folder_andro";
    req1.name_len = strlen(req1.name);
    req1.is_directory = true;
    req1.buffer_size = 0; // Folder buffer size 0
    
    int8_t res1 = write(&req1);
    if (res1 == 0) printf("  -> SUKSES!\n");
    else printf("  -> GAGAL! Error code: %d\n", res1);


    // ==========================================
    // TEST 2: Membuat folder duplikat (Harus ditolak)
    // ==========================================
    printf("Test 2: Membuat folder duplikat 'folder_andro'...\n");
    int8_t res2 = write(&req1);
    if (res2 == 1) printf("  -> SUKSES! (Berhasil ditolak dengan error 1)\n");
    else printf("  -> GAGAL! Error code: %d\n", res2);


    // ==========================================
    // TEST 3: Membuat file 'file_andro.txt'
    // ==========================================
    printf("Test 3: Membuat file 'file_andro.txt'...\n");
    struct EXT2DriverRequest req3 = {0};
    char* file_content = "Halo, ini file buatan Andro untuk test OS!";
    
    req3.parent_inode = 1; // Root directory
    req3.name = "file_andro.txt";
    req3.name_len = strlen(req3.name);
    req3.is_directory = false;
    req3.buf = file_content;
    req3.buffer_size = strlen(file_content); // Ukuran teks
    
    int8_t res3 = write(&req3);
    if (res3 == 0) printf("  -> SUKSES!\n");
    else printf("  -> GAGAL! Error code: %d\n", res3);

    printf("=== Test Selesai ===\n");
    printf("Silakan buka file bin/storage.bin menggunakan Hex Editor di VSCode untuk verifikasi!\n");

    return 0;
}