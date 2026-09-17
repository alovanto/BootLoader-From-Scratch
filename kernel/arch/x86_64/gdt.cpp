#include "gdt.hpp"
#include "tss.hpp"

namespace {

// 3 slot 8-byte (null, code64, data64) + 2 slot 8-byte gộp thành 1 vùng
// 16-byte cho TSS descriptor = tổng 5 slot GdtEntry.
GdtEntry gdt[5];
GdtPointer gdtp;

void set_entry(int index, uint32_t base, uint32_t limit, uint8_t access, uint8_t flags) {
    gdt[index].limit_low   = static_cast<uint16_t>(limit & 0xFFFF);
    gdt[index].base_low    = static_cast<uint16_t>(base & 0xFFFF);
    gdt[index].base_mid    = static_cast<uint8_t>((base >> 16) & 0xFF);
    gdt[index].access      = access;
    gdt[index].granularity = static_cast<uint8_t>((flags & 0xF0) | ((limit >> 16) & 0x0F));
    gdt[index].base_high   = static_cast<uint8_t>((base >> 24) & 0xFF);
}

} // namespace

// Định nghĩa trong gdt_flush.asm — lgdt rồi reload toàn bộ segment register
// (bao gồm CS, việc CPU không cho `mov cs` trực tiếp làm được).
extern "C" void gdt_flush(uint64_t gdtp_addr, uint16_t code_selector, uint16_t data_selector);

void gdt_init() {
    // Long Mode phần lớn bỏ qua base/limit thật của code/data segment (bắt
    // buộc flat model), nhưng vẫn cần entry hợp lệ để CPU đọc DPL/type/L-bit.
    set_entry(0, 0, 0, 0x00, 0x00);                 // null descriptor — bắt buộc phải rỗng

    // access=0x9A: Present=1, DPL=0, S=1(code/data), Execute=1, Read=1
    // flags=0xA0 : G=1 (granularity), L=1 (Long Mode code — QUAN TRỌNG,
    //              đây là bit báo CPU đây là code 64-bit, khác hẳn code32 cũ)
    set_entry(1, 0, 0xFFFFF, 0x9A, 0xA0);            // kernel code64 -> KERNEL_CODE_SELECTOR (0x08)

    // access=0x92: Present=1, DPL=0, S=1, Write=1
    set_entry(2, 0, 0xFFFFF, 0x92, 0xC0);            // kernel data64 -> KERNEL_DATA_SELECTOR (0x10)

    // Entry 3 và 4 gộp lại thành 1 TssDescriptor 16-byte (xem gdt.hpp)
    TssDescriptor* tss_desc = reinterpret_cast<TssDescriptor*>(&gdt[3]);
    tss_install_descriptor(tss_desc);

    gdtp.limit = sizeof(gdt) - 1;
    gdtp.base  = reinterpret_cast<uint64_t>(&gdt[0]);

    gdt_flush(reinterpret_cast<uint64_t>(&gdtp), KERNEL_CODE_SELECTOR, KERNEL_DATA_SELECTOR);

    // PHẢI gọi sau gdt_flush (TSS descriptor cần đã nằm trong GDT đang active)
    tss_load();
}
