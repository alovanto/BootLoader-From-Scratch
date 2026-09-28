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

// -------------------------------------------------------------------------
// Bản chụp trạng thái CPU tại thời điểm ngắt/exception xảy ra.
//
// Thứ tự field PHẢI khớp CHÍNH XÁC thứ tự push trong isr_stubs.asm
// (xem `isr_common`): field đầu tiên ứng với lần push CUỐI CÙNG, vì stack mọc
// xuống nên push sau nằm ở địa chỉ thấp hơn.
//
// QUAN TRỌNG — vì sao có rsp/ss:
// Ở Protected Mode 32-bit, CPU chỉ push SS:ESP khi có ĐỔI MỨC ĐẶC QUYỀN.
// Ở Long Mode thì KHÁC: CPU luôn push đủ 5 giá trị (SS, RSP, RFLAGS, CS, RIP)
// kể cả khi ngắt xảy ra ring 0 → ring 0, và còn căn RSP về bội số 16 trước khi
// push (Intel SDM Vol.3 §6.14.2). Struct này từng thiếu 2 field cuối; code vẫn
// chạy vì `iretq` tự pop đủ, nhưng chẩn đoán mất đi RSP — đúng thứ cần nhất để
// phát hiện tràn stack.
// -------------------------------------------------------------------------
struct InterruptFrame {
    // 15 thanh ghi do isr_common tự push
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    // Do stub push để mọi vector có cùng layout
    uint64_t vector;
    uint64_t error_code;   // error code thật của CPU, hoặc 0 nếu vector này không có
    // Do CPU push — LUÔN đủ 5 ô ở Long Mode
    uint64_t rip, cs, rflags, rsp, ss;
};

static_assert(sizeof(InterruptFrame) == 22 * 8, "InterruptFrame phai khop so lan push trong isr_common");

// Ngắt/exception đến từ ring 3 hay không. Hiện luôn false (kernel chạy 100%
// ring 0), nhưng mọi chỗ kiểm tra "lỗi này của user hay của kernel" nên hỏi
// qua hàm này để khi có user mode thì không phải đi sửa từng nơi.
inline bool frame_from_user(const InterruptFrame* frame) { return (frame->cs & 3) != 0; }

// Số vector trong IDT của x86_64. 0-31 là exception của CPU, 32-47 dành cho
// IRQ phần cứng sau khi remap PIC, phần còn lại chưa dùng nhưng vẫn phải có
// handler: một vector không được đăng ký mà bị kích hoạt sẽ gây #GP với chẩn
// đoán rất khó hiểu.
constexpr int IDT_ENTRY_COUNT = 256;

// Vector CPU exception được chạy trên stack riêng qua cơ chế IST (xem tss.cpp).
// Ba loại này có thể xảy ra ở BẤT KỲ đâu, kể cả khi stack hiện tại đã hỏng.
constexpr uint8_t VECTOR_NMI           = 2;
constexpr uint8_t VECTOR_DOUBLE_FAULT  = 8;
constexpr uint8_t VECTOR_MACHINE_CHECK = 18;

constexpr uint8_t IST_DOUBLE_FAULT  = 1;
constexpr uint8_t IST_NMI           = 2;
constexpr uint8_t IST_MACHINE_CHECK = 3;

void idt_init();
void idt_set_gate(uint8_t vector, uint64_t handler, uint16_t selector, uint8_t ist, uint8_t type_attr);

// Điểm vào C++ duy nhất cho mọi ngắt/exception, được isr_common (asm) gọi vào
extern "C" void interrupt_dispatch(InterruptFrame* frame);

// Bảng 256 con trỏ stub, định nghĩa trong isr_stubs.asm
extern "C" void* isr_stub_table[IDT_ENTRY_COUNT];
