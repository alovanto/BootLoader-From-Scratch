#include "tss.hpp"
#include "lib/string.hpp"

namespace {

Tss tss;

// Stack RIÊNG cho từng loại exception "không thể tin stack hiện tại".
// Ba loại dưới đây có thể xảy ra ở BẤT KỲ thời điểm nào, kể cả khi RSP đang
// trỏ vào vùng rác — nên CPU phải được ép chuyển sang một vùng sạch trước khi
// gọi handler, nếu không chính handler sẽ gây lỗi tiếp → triple fault.
constexpr uint32_t IST_STACK_SIZE = 8192;

alignas(16) uint8_t double_fault_stack[IST_STACK_SIZE];
alignas(16) uint8_t nmi_stack[IST_STACK_SIZE];
alignas(16) uint8_t machine_check_stack[IST_STACK_SIZE];

}  // namespace

void tss_install_descriptor(TssDescriptor* out_descriptor) {
    // Xoá sạch TSS trước khi điền — mọi field không dùng tới (RSP1, RSP2,
    // IST4-7...) phải là 0, không được để rác từ bộ nhớ chưa khởi tạo.
    memset(&tss, 0, sizeof(Tss));

    // Đỉnh stack = địa chỉ CAO NHẤT của vùng đệm, vì stack x86 mọc xuống.
    tss.ist1 = reinterpret_cast<uint64_t>(double_fault_stack + IST_STACK_SIZE);
    tss.ist2 = reinterpret_cast<uint64_t>(nmi_stack + IST_STACK_SIZE);
    tss.ist3 = reinterpret_cast<uint64_t>(machine_check_stack + IST_STACK_SIZE);

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

void tss_set_rsp0(uint64_t rsp0) {
    tss.rsp0 = rsp0;
}
