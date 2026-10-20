#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "header/filesystem/ext2.h"

FILE *disk_file;

// Mock fungsi portio (karena kita ngetes di komputer biasa, bukan di QEMU)
void out16(uint16_t port, uint16_t data) {}
uint16_t in16(uint16_t port) { return 0; }
void out(uint16_t port, uint8_t data) {}
uint8_t in(uint16_t port) { return 0; }

// Mock read_blocks & write_blocks agar langsung baca/tulis ke file storage.bin
void read_blocks(void *ptr, uint32_t logical_block_address, uint8_t block_count) {
    fseek(disk_file, logical_block_address * 512, SEEK_SET);
    fread(ptr, 512, block_count, disk_file);
}

void write_blocks(const void *ptr, uint32_t logical_block_address, uint8_t block_count) {
    fseek(disk_file, logical_block_address * 512, SEEK_SET);
    fwrite(ptr, 512, block_count, disk_file);
    fflush(disk_file);
}

int main() {
    printf("=== Mulai Test Write EXT2 (Tugas P4) ===\n");

    // 1. Buka file storage.bin (Asumsikan ada di folder bin)
    disk_file = fopen("bin/storage.bin", "r+b");
    if (!disk_file) {
        printf("GAGAL: Tidak bisa membuka storage.bin. Pastikan pathnya benar.\n");
        return 1;
    }

    // 2. Inisialisasi filesystem (ini akan memanggil create_ext2() buatan P2)
    initialize_filesystem_ext2();

    // 3. Test Case 1: Membuat Folder Baru ("folder_andro") di Root (inode 1)
    struct EXT2DriverRequest req_folder = {
        .buf = NULL,
        .name = "folder_andro",
        .name_len = 12,
        .parent_inode = 1, // Root
        .buffer_size = 0,
        .is_directory = true
    };
    
    printf("Test 1: Membuat folder 'folder_andro'...\n");
    int8_t res1 = write(&req_folder);
    if (res1 == 0) printf("  -> SUKSES!\n");
    else printf("  -> GAGAL! Error code: %d\n", res1);

    // 4. Test Case 2: Membuat Folder Duplikat (Harusnya Gagal/Error 1)
    printf("Test 2: Membuat folder duplikat 'folder_andro'...\n");
    int8_t res2 = write(&req_folder);
    if (res2 == 1) printf("  -> SUKSES! (Berhasil ditolak dengan error 1)\n");
    else printf("  -> GAGAL! Harusnya error 1, tapi dapat: %d\n", res2);

    // 5. Test Case 3: Membuat File Baru ("file_andro.txt") di Root
    char file_content[] = "Halo, ini file buatan Andro untuk test OS!";
    struct EXT2DriverRequest req_file = {
        .buf = file_content,
        .name = "file_andro.txt",
        .name_len = 14,
        .parent_inode = 1, // Root
        .buffer_size = sizeof(file_content),
        .is_directory = false
    };

    printf("Test 3: Membuat file 'file_andro.txt'...\n");
    int8_t res3 = write(&req_file);
    if (res3 == 0) printf("  -> SUKSES!\n");
    else printf("  -> GAGAL! Error code: %d\n", res3);

    fclose(disk_file);
    printf("=== Test Selesai ===\n");
    printf("Silakan buka file bin/storage.bin menggunakan Hex Editor di VSCode untuk verifikasi!\n");

    return 0;
}