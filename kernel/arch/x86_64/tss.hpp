#pragma once
#include <cstdint>
#include "gdt.hpp"

// Task State Segment — định dạng 64-bit, đúng 104 byte theo Intel SDM Vol 3
// mục 7.7. Ở Long Mode, TSS không còn dùng để chuyển task bằng phần cứng
// (vai trò gốc thời 32-bit) — CPU chỉ đọc từ nó 2 nhóm giá trị:
//   - RSP0-2 : stack CPU tự nạp khi đổi mức đặc quyền (cần khi có ring 3)
//   - IST1-7 : stack CPU ép dùng cho exception có IST != 0 trong IDT gate
struct Tss {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;   // <- Double Fault (vector 8) sẽ dùng đúng slot này
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;
} __attribute__((packed));

static_assert(sizeof(Tss) == 104, "TSS phai dung 104 byte theo Intel SDM");

// Điền descriptor 16-byte vào đúng vị trí GDT do gdt.cpp cung cấp.
void tss_install_descriptor(TssDescriptor* out_descriptor);

// Nạp Task Register bằng lệnh `ltr` — phải gọi SAU khi GDT chứa TSS
// descriptor đã active (đã lgdt xong).
void tss_load();
