#pragma once
#include <cstdint>

// Một entry (gate) trong IDT — định dạng x86_64, 16 byte mỗi entry.
// Đây là "hợp đồng" mà CPU đọc trực tiếp, layout phải khớp CHÍNH XÁC
// với Intel/AMD manual, không được sai một byte nào.
struct IdtEntry {
    uint16_t offset_low;   // bit 0-15 của địa chỉ handler
    uint16_t selector;     // code segment selector (trỏ vào GDT)
    uint8_t  ist;          // Interrupt Stack Table index (0 = dùng stack hiện tại)
    uint8_t  type_attr;    // present, DPL, loại gate (0x8E = interrupt gate, ring0)
    uint16_t offset_mid;   // bit 16-31 của địa chỉ handler
    uint32_t offset_high;  // bit 32-63 của địa chỉ handler
    uint32_t zero;         // reserved, phải bằng 0
} __attribute__((packed));

// Cấu trúc truyền cho lệnh `lidt` — giống hệt GDT descriptor đã dùng trong boot.asm
struct IdtPointer {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

// Bản chụp thanh ghi tại thời điểm exception xảy ra.
// QUAN TRỌNG: thứ tự các field ở đây phải khớp CHÍNH XÁC với thứ tự
// isr_stubs.asm push lên stack (xem isr_common trong file đó).
// Không có RSP/SS vì kernel hiện tại chạy 100% ở ring 0 (xem giải thích ở trên).
struct Registers {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector;       // số hiệu exception (0-31)
    uint64_t error_code;   // error code thật của CPU, hoặc 0 nếu vector này không có
    uint64_t rip, cs, rflags; // CPU tự push 3 field này (ring0 → ring0, không đổi mức đặc quyền)
};

void idt_init();
void idt_set_gate(uint8_t vector, uint64_t handler, uint16_t selector, uint8_t ist, uint8_t type_attr);

// Định nghĩa thật nằm trong interrupt_handlers.cpp, được isr_common (asm) gọi vào
extern "C" void isr_handler(Registers* regs);

// Bảng 32 con trỏ hàm, định nghĩa trong isr_stubs.asm
extern "C" void* isr_stub_table[32];
