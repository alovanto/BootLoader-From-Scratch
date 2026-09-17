#pragma once
#include <cstdint>

// Địa chỉ vật lý CỐ ĐỊNH nơi boot.asm (Stage2, Real Mode) ghi bản đồ RAM
// dò được qua BIOS INT 15h, EAX=0xE820 — PHẢI khớp `E820_SEG:E820_OFF`
// trong boot/boot.asm (hiện = segment 0x2000, offset 0 => vật lý 0x20000).
// Đây là hợp đồng boot protocol: đổi một bên phải đổi bên kia (giống cách
// KERNEL_PHYS_ADDR đã được ràng buộc giữa boot.asm và linker.ld).
//
// Vùng nhớ này nằm trong 1GB đầu mà boot.asm đã identity-map, nên kernel
// đọc thẳng bằng con trỏ vật lý bình thường, không cần map() gì thêm.
constexpr uint64_t E820_MAP_PHYS_ADDR = 0x20000;

// Type theo đúng chuẩn BIOS E820 (Intel/ACPI spec), không phải tự đặt —
// Physical Frame Allocator (mm/pmm.cpp, sắp làm) sẽ chỉ cấp phát từ vùng
// Usable, mọi type khác phải bị loại trừ tuyệt đối.
enum class E820Type : uint32_t {
    Usable          = 1,  // RAM dùng được
    Reserved        = 2,  // BIOS/firmware giữ, không được đụng
    AcpiReclaimable = 3,  // ACPI table — chỉ tái sử dụng được SAU KHI đọc xong ACPI
    AcpiNvs         = 4,  // ACPI NVS, không được đụng
    Bad             = 5,  // RAM lỗi phần cứng, không được đụng
};

// Layout PHẢI khớp CHÍNH XÁC 20 byte mà detect_memory_e820 trong boot.asm
// ghi cho mỗi entry (base:8, length:8, type:4) — không thêm/bớt field nào
// mà không sửa boot.asm tương ứng.
struct E820Entry {
    uint64_t base;
    uint64_t length;
    uint32_t type;
} __attribute__((packed));

static_assert(sizeof(E820Entry) == 20, "E820Entry phai dung 20 byte khop boot.asm");

// Số entry thật bootloader dò được (4 byte đầu buffer). Có thể bằng 0 nếu
// BIOS/QEMU không hỗ trợ E820 — gọi trước mọi thứ khác để biết giới hạn.
uint32_t e820_entry_count();

// Trỏ tới entry thứ `index` (0-based). KHÔNG kiểm tra biên — luôn gọi
// e820_entry_count() trước và tự đảm bảo index hợp lệ.
const E820Entry* e820_entry(uint32_t index);

// In toàn bộ bản đồ RAM ra VGA text buffer — bước xác minh bằng mắt trên
// QEMU trước khi Physical Frame Allocator dùng dữ liệu này làm input thật
// (đúng nguyên tắc "mỗi bước phải thấy kết quả trên QEMU" ở CLAUDE.md).
void e820_dump();
