#include "gdt.hpp"
#include "tss.hpp"

namespace {

// 6 slot 8-byte (null, kernel code/data, user code32/data/code64) + 2 slot
// gộp thành vùng 16-byte cho TSS descriptor = tổng 8 slot GdtEntry.
GdtEntry gdt[8];
GdtPointer gdtp;

void set_entry(int index, uint32_t base, uint32_t limit, uint8_t access, uint8_t flags) {
    gdt[index].limit_low   = static_cast<uint16_t>(limit & 0xFFFF);
    gdt[index].base_low    = static_cast<uint16_t>(base & 0xFFFF);
    gdt[index].base_mid    = static_cast<uint8_t>((base >> 16) & 0xFF);
    gdt[index].access      = access;
    gdt[index].granularity = static_cast<uint8_t>((flags & 0xF0) | ((limit >> 16) & 0x0F));
    gdt[index].base_high   = static_cast<uint8_t>((base >> 24) & 0xFF);
}

}  // namespace

// Định nghĩa trong gdt_flush.asm — lgdt rồi reload toàn bộ segment register
// (bao gồm CS, việc CPU không cho `mov cs` trực tiếp làm được).
extern "C" void gdt_flush(uint64_t gdtp_addr, uint16_t code_selector, uint16_t data_selector);

void gdt_init() {
    // Long Mode phần lớn bỏ qua base/limit thật của code/data segment (bắt
    // buộc flat model), nhưng vẫn cần entry hợp lệ để CPU đọc DPL/type/L-bit.
    set_entry(0, 0, 0, 0x00, 0x00);                 // null descriptor — bắt buộc phải rỗng

    // access=0x9A: Present, DPL=0, S=1 (code/data), Execute, Readable
    // flags=0xA0 : G=1, L=1 (long mode code — bit báo CPU đây là code 64-bit)
    set_entry(1, 0, 0xFFFFF, 0x9A, 0xA0);            // 0x08 kernel code64

    // access=0x92: Present, DPL=0, S=1, Writable
    set_entry(2, 0, 0xFFFFF, 0x92, 0xC0);            // 0x10 kernel data

    // DPL=3 cho phần user. Ba entry này chưa dùng khi kernel còn chạy 100%
    // ring 0, nhưng vị trí của chúng bị công thức SYSRET ràng buộc (xem gdt.hpp).
    set_entry(3, 0, 0xFFFFF, 0xFA, 0xC0);            // 0x18 user code32 (giữ chỗ, D=1)
    set_entry(4, 0, 0xFFFFF, 0xF2, 0xC0);            // 0x20 user data
    set_entry(5, 0, 0xFFFFF, 0xFA, 0xA0);            // 0x28 user code64 (L=1)

    // Entry 6 và 7 gộp lại thành 1 TssDescriptor 16-byte → selector 0x30
    TssDescriptor* tss_desc = reinterpret_cast<TssDescriptor*>(&gdt[6]);
    tss_install_descriptor(tss_desc);

    gdtp.limit = sizeof(gdt) - 1;
    gdtp.base  = reinterpret_cast<uint64_t>(&gdt[0]);

    gdt_flush(reinterpret_cast<uint64_t>(&gdtp), KERNEL_CODE_SELECTOR, KERNEL_DATA_SELECTOR);

    // PHẢI gọi sau gdt_flush (TSS descriptor cần đã nằm trong GDT đang active)
    tss_load();
}
