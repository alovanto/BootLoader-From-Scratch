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
// kernel tự dựng GDT RIÊNG.
//
// Thứ tự các entry user KHÔNG tuỳ ý: lệnh SYSRET (dùng khi quay về ring 3 ở
// bước user mode) tính selector theo công thức CỐ ĐỊNH từ thanh ghi MSR STAR:
//     SS = STAR[63:48] + 8       CS = STAR[63:48] + 16
// nên bắt buộc phải xếp: user code32 (giữ chỗ) → user data → user code64.
// Chốt đúng thứ tự ngay từ bây giờ để sau này thêm SYSCALL không phải xáo lại
// GDT (xáo lại đồng nghĩa phải sửa cả idt.cpp và mọi chỗ hardcode selector).
// ---------------------------------------------------------------------
constexpr uint16_t KERNEL_CODE_SELECTOR = 0x08;
constexpr uint16_t KERNEL_DATA_SELECTOR = 0x10;
constexpr uint16_t USER_BASE_SELECTOR   = 0x18;       // user code32, chỉ để giữ chỗ cho công thức SYSRET
constexpr uint16_t USER_DATA_SELECTOR   = 0x20 | 3;   // = 0x23 (RPL 3)
constexpr uint16_t USER_CODE_SELECTOR   = 0x28 | 3;   // = 0x2B (RPL 3)
constexpr uint16_t TSS_SELECTOR         = 0x30;       // chiếm 0x30 và 0x38 (descriptor 16 byte)

static_assert(USER_DATA_SELECTOR == ((USER_BASE_SELECTOR + 8) | 3),
              "SYSRET nap SS = STAR[63:48] + 8 — thu tu entry user trong GDT dang sai");
static_assert(USER_CODE_SELECTOR == ((USER_BASE_SELECTOR + 16) | 3),
              "SYSRET nap CS = STAR[63:48] + 16 — thu tu entry user trong GDT dang sai");

void gdt_init();
