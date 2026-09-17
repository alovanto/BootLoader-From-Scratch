#include "idt.hpp"
#include "gdt.hpp"

static IdtEntry idt[256];   // 256 entry theo chuẩn x86_64, ta chỉ điền 32 exception đầu
static IdtPointer idtp;

// 0x8E = 1000_1110b:
//   bit 7    = Present = 1
//   bit 6-5  = DPL = 00 (chỉ ring 0 được gọi trực tiếp qua int; exception CPU tự gọi được bất kể DPL)
//   bit 4    = 0 (system descriptor)
//   bit 3-0  = 1110 = 64-bit Interrupt Gate (tự động tắt cờ IF khi vào, tránh nested interrupt)
static constexpr uint8_t GATE_INTERRUPT_RING0 = 0x8E;

// Vector CPU exception dùng error code thật (khớp đúng danh sách trong isr_stubs.asm)
static constexpr uint8_t VECTOR_DOUBLE_FAULT = 8;

void idt_set_gate(uint8_t vector, uint64_t handler, uint16_t selector, uint8_t ist, uint8_t type_attr) {
    idt[vector].offset_low  = static_cast<uint16_t>(handler & 0xFFFF);
    idt[vector].selector    = selector;
    idt[vector].ist         = ist;
    idt[vector].type_attr   = type_attr;
    idt[vector].offset_mid  = static_cast<uint16_t>((handler >> 16) & 0xFFFF);
    idt[vector].offset_high = static_cast<uint32_t>((handler >> 32) & 0xFFFFFFFF);
    idt[vector].zero        = 0;
}

void idt_init() {
    idtp.limit = sizeof(idt) - 1;
    idtp.base  = reinterpret_cast<uint64_t>(&idt[0]);

    // Đăng ký 32 exception handler (0-31). Vector 32+ dành cho hardware
    // interrupt (PIC/APIC) — cố tình chưa đụng tới ở bước "lưới an toàn" này.
    for (int vector = 0; vector < 32; vector++) {
        // IST=1 CHỈ dành cho Double Fault: nếu nguyên nhân double fault là
        // stack đã hỏng (stack overflow), dùng chung stack cũ để gọi handler
        // sẽ fault lần nữa -> triple fault. IST ép CPU chuyển sang stack
        // riêng sạch trong tss.cpp bất kể RSP hiện tại đang là gì.
        // Mọi vector khác vẫn dùng IST=0 (giữ nguyên stack hiện tại).
        const uint8_t ist = (vector == VECTOR_DOUBLE_FAULT) ? 1 : 0;

        idt_set_gate(
            static_cast<uint8_t>(vector),
            reinterpret_cast<uint64_t>(isr_stub_table[vector]),
            KERNEL_CODE_SELECTOR,   // từ gdt.hpp — GDT của kernel, KHÔNG phải GDT tạm của boot.asm nữa
            ist,
            GATE_INTERRUPT_RING0
        );
    }

    asm volatile("lidt %0" : : "m"(idtp));
}
