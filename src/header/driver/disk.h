#ifndef _DISK_H
#define _DISK_H

#include <stdint.h>

/* ATA PIO status register bits */
#define ATA_STATUS_BSY 0x80
#define ATA_STATUS_RDY 0x40
#define ATA_STATUS_DRQ 0x08
#define ATA_STATUS_DF  0x20
#define ATA_STATUS_ERR 0x01

#define BLOCK_SIZE      512
#define HALF_BLOCK_SIZE (BLOCK_SIZE / 2)

/**
 * BlockBuffer, buffer berukuran tepat 1 block (512 bytes)
 */
struct BlockBuffer {
    uint8_t buf[BLOCK_SIZE];
} __attribute__((packed));

/**
 * Baca block dari disk (ATA PIO, blocking)
 * @param ptr                   Pointer tujuan di memory
 * @param logical_block_address Nomor block awal (LBA)
 * @param block_count           Jumlah block yang dibaca
 */
void read_blocks(void *ptr, uint32_t logical_block_address, uint8_t block_count);

/**
 * Tulis block ke disk (ATA PIO, blocking)
 * @param ptr                   Pointer sumber data di memory
 * @param logical_block_address Nomor block awal (LBA)
 * @param block_count           Jumlah block yang ditulis
 */
void write_blocks(const void *ptr, uint32_t logical_block_address, uint8_t block_count);

#endif