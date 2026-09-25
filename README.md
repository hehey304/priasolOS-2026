
# priasolOS - IF2130 Sistem Operasi 2026/2027

<div align="center">
  <img src="mascot.jpg" alt="Maskot Kelompok" width="450"/>
  <br>
  <em>Maskot Kelompok priasolOS</em>
</div>

---

## Daftar Isi
1. [Anggota Kelompok](#anggota-kelompok)
2. [Deskripsi Proyek](#deskripsi-proyek)
3. [Fitur yang Dibuat](#fitur-yang-dibuat)
4. [Prasyarat dan Dependensi](#prasyarat-dan-dependensi)
5. [Cara Build dan Run](#cara-build-dan-run)
6. [Struktur Direktori](#struktur-direktori)

---

## Anggota Kelompok
**Nama Kelompok:** priasolOS

| No. | Nama Lengkap | NIM |
| :---: | :--- | :---: |
| 1. | Andro Irsa Syafiq | 13525129 |
| 2. | Chatima Anandakhorita | 13525133 |
| 3. | Muhammad Nur Fikri Hariyawan | 13525145 |
| 4. | Athallah Nanda Andita | 13525148 |

---

## Deskripsi Proyek
Sistem operasi 32-bit berbasis arsitektur x86 yang dikembangkan untuk memenuhi Tugas Besar IF2130 Sistem Operasi Semester Ganjil 2026/2027. Kernel ini menggunakan bootloader Multiboot GRUB dan berjalan pada lingkungan emulasi QEMU.

---


## Fitur yang Dibuat

### Milestone 0: Bootstrapping & Setup

### Milestone 1: Hardware Abstraction & I/O

---

## Prasyarat dan Dependensi
Pastikan utilitas berikut telah terpasang di sistem Linux/WSL:
- `gcc` (dengan dukungan `-m32`)
- `nasm`
- `ld`
- `genisoimage`
- `qemu-system-i386`
- `make`

---

## Cara Build dan Run

1. **Kompilasi dan Jalankan Otomatis:**
   ```bash
   make run

2. **Membersihkan File Kompilasi:**
   ```bash
   make clean

---

## Struktur Direktori

```text
.
├── .kit/             
├── .vscode/          
├── bin/                   
├── other/                 
├── src/
│   ├── header/                
│   ├── stdlib/       
├── Makefile               
├── README.md         
└── mascot.jpg         
```

---