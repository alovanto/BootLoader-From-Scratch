#pragma once
#include <cstdint>

// Descriptor thường (8 byte) — dùng cho code/data segment.
struct GdtEntry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

// Descriptor hệ thống (16 byte) — CHỈ dùng cho TSS ở Long Mode, vì cần chứa
// base address đầy đủ 64-bit mà descriptor 8-byte thường không đủ chỗ.
// Vì chiếm 16 byte, nó "ăn" liền 2 slot trong mảng GdtEntry.
struct TssDescriptor {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  limit_high_flags;
    uint8_t  base_high;
    uint32_t base_upper;
    uint32_t reserved;
} __attribute__((packed));

struct GdtPointer {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

// ---------------------------------------------------------------------
// Layout GDT CỦA KERNEL — khác với GDT tạm trong boot.asm.
//
// boot.asm dùng GDT của nó (null=0x00, code32=0x08, data32=0x10,
// code64=0x18) chỉ đủ để tồn tại tới lúc bàn giao cho kernel. Từ đây,
// kernel tự dựng GDT RIÊNG, đầy đủ hơn (có chỗ cho TSS) — đây là lý do
// selector code kernel đổi từ 0x18 (thời bootloader) sang 0x08 (từ đây
// về sau). idt.cpp PHẢI dùng đúng hằng số dưới đây, không hardcode lại.
// ---------------------------------------------------------------------
constexpr uint16_t KERNEL_CODE_SELECTOR = 0x08;
constexpr uint16_t KERNEL_DATA_SELECTOR = 0x10;
constexpr uint16_t TSS_SELECTOR         = 0x18;  // chiếm 0x18 và 0x20 (16 byte)

void gdt_init();
