#include "tss.hpp"

namespace {

Tss tss;

// Stack RIÊNG, chỉ dành cho Double Fault handler. Không subsystem nào khác
// được đụng vào vùng này — đó chính là lý do nó tồn tại: đảm bảo luôn có
// một chỗ "sạch" để chạy handler, bất kể stack chính đang hỏng cỡ nào.
constexpr uint32_t DOUBLE_FAULT_STACK_SIZE = 8192; // 8KB — dư dả cho 1 hàm handler đơn giản
alignas(16) uint8_t double_fault_stack[DOUBLE_FAULT_STACK_SIZE];

} // namespace

void tss_install_descriptor(TssDescriptor* out_descriptor) {
    // Xoá sạch TSS trước khi điền — mọi field không dùng tới (RSP1, RSP2,
    // IST2-7...) phải là 0, không được để rác từ bộ nhớ chưa khởi tạo.
    uint8_t* raw = reinterpret_cast<uint8_t*>(&tss);
    for (uint32_t i = 0; i < sizeof(Tss); i++) {
        raw[i] = 0;
    }

    // Đỉnh stack = địa chỉ CAO NHẤT của vùng đệm, vì stack x86 mọc xuống.
    tss.ist1 = reinterpret_cast<uint64_t>(double_fault_stack + DOUBLE_FAULT_STACK_SIZE);

    // Không dùng I/O Permission Bitmap ở bước này — trỏ offset ra ngoài
    // giới hạn TSS nghĩa là "không có bitmap nào cả".
    tss.iopb_offset = sizeof(Tss);

    const uint64_t base  = reinterpret_cast<uint64_t>(&tss);
    const uint32_t limit = sizeof(Tss) - 1;

    out_descriptor->limit_low        = static_cast<uint16_t>(limit & 0xFFFF);
    out_descriptor->base_low         = static_cast<uint16_t>(base & 0xFFFF);
    out_descriptor->base_mid         = static_cast<uint8_t>((base >> 16) & 0xFF);
    out_descriptor->access           = 0x89; // Present=1, DPL=0, type=1001 (Available 64-bit TSS)
    out_descriptor->limit_high_flags = static_cast<uint8_t>((limit >> 16) & 0x0F);
    out_descriptor->base_high        = static_cast<uint8_t>((base >> 24) & 0xFF);
    out_descriptor->base_upper       = static_cast<uint32_t>(base >> 32);
    out_descriptor->reserved         = 0;
}

void tss_load() {
    asm volatile("ltr %0" : : "r"(static_cast<uint16_t>(TSS_SELECTOR)));
}
