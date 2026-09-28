#include "idt.hpp"
#include "gdt.hpp"
#include "cpu.hpp"

namespace {

IdtEntry idt[IDT_ENTRY_COUNT];
IdtPointer idtp;

// 0x8E = 1000_1110b:
//   bit 7    = Present = 1
//   bit 6-5  = DPL = 00 (chỉ ring 0 được gọi trực tiếp qua `int N`;
//                        exception thì CPU tự gọi được bất kể DPL)
//   bit 4    = 0 (system descriptor)
//   bit 3-0  = 1110 = 64-bit Interrupt Gate
//
// Chọn Interrupt Gate (không phải Trap Gate) vì nó tự động xoá cờ IF khi vào
// handler: ngắt không lồng nhau, nên không cần lo stack bị đào sâu vô hạn.
constexpr uint8_t GATE_INTERRUPT_RING0 = 0x8E;

// Vector nào chạy trên stack riêng (IST) — xem tss.cpp để biết các stack này.
uint8_t ist_index_for(int vector) {
    switch (vector) {
    case VECTOR_DOUBLE_FAULT:  return IST_DOUBLE_FAULT;
    case VECTOR_NMI:           return IST_NMI;
    case VECTOR_MACHINE_CHECK: return IST_MACHINE_CHECK;
    default:                   return 0;   // dùng chung stack hiện tại
    }
}

}  // namespace

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

    // Đăng ký TẤT CẢ 256 vector. Vector chưa dùng vẫn trỏ tới stub chung —
    // handler C++ sẽ báo "vector không mong đợi" thay vì để CPU sinh #GP.
    for (int vector = 0; vector < IDT_ENTRY_COUNT; vector++) {
        idt_set_gate(
            static_cast<uint8_t>(vector),
            reinterpret_cast<uint64_t>(isr_stub_table[vector]),
            KERNEL_CODE_SELECTOR,   // từ gdt.hpp — GDT của kernel, KHÔNG phải GDT tạm của boot.asm
            ist_index_for(vector),
            GATE_INTERRUPT_RING0);
    }

    asm volatile("lidt %0" : : "m"(idtp));
}
