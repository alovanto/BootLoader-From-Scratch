#include "idt.hpp"

namespace {

volatile uint16_t* const VGA = reinterpret_cast<volatile uint16_t*>(0xB8000);
constexpr int VGA_COLS = 80;
int vga_row = 0;

// In một dòng text ra VGA text buffer (mỗi ký tự = 2 byte: ascii + màu)
void vga_print(const char* str, uint8_t color) {
    int col = 0;
    while (*str && col < VGA_COLS) {
        VGA[vga_row * VGA_COLS + col] = static_cast<uint16_t>(color) << 8 | static_cast<uint8_t>(*str);
        str++;
        col++;
    }
    vga_row++;
}

// Chuyển 1 số 64-bit thành chuỗi hex, không dùng bất kỳ hàm libc nào
// (chưa có heap/libc ở bước này — mọi buffer đều nằm trên stack).
void to_hex(char* out, uint64_t value) {
    const char* digits = "0123456789ABCDEF";
    out[0] = '0';
    out[1] = 'x';
    for (int i = 0; i < 16; i++) {
        out[2 + i] = digits[(value >> ((15 - i) * 4)) & 0xF];
    }
    out[18] = '\0';
}

// Đọc thanh ghi CR2 — CPU tự động lưu ĐỊA CHỈ ẢO gây ra page fault vào đây
// (chỉ đúng ngữ nghĩa với vector 14; các exception khác không đụng CR2).
uint64_t read_cr2() {
    uint64_t value;
    asm volatile("mov %%cr2, %0" : "=r"(value));
    return value;
}

const char* exception_name(uint64_t vector) {
    static const char* names[32] = {
        "Divide Error (#DE)", "Debug (#DB)", "NMI", "Breakpoint (#BP)",
        "Overflow (#OF)", "BOUND Range (#BR)", "Invalid Opcode (#UD)", "Device Not Available (#NM)",
        "Double Fault (#DF)", "Coprocessor Overrun", "Invalid TSS (#TS)", "Segment Not Present (#NP)",
        "Stack Fault (#SS)", "General Protection Fault (#GP)", "Page Fault (#PF)", "Reserved",
        "x87 FP Error (#MF)", "Alignment Check (#AC)", "Machine Check (#MC)", "SIMD FP (#XM)",
        "Virtualization (#VE)", "Control Protection (#CP)", "Reserved", "Reserved",
        "Reserved", "Reserved", "Reserved", "Reserved",
        "Reserved", "Reserved", "Reserved", "Reserved"
    };
    return vector < 32 ? names[vector] : "Unknown";
}

// Giải mã 3 bit thấp của error code riêng cho Page Fault (Intel SDM Vol 3, 4.7)
void print_page_fault_detail(uint64_t error_code) {
    char buf[19];

    vga_print((error_code & 0x1) ? "  - Loai: Protection violation (page co ton tai)"
                                  : "  - Loai: Page khong ton tai (Not Present)", 0x0E);
    vga_print((error_code & 0x2) ? "  - Thao tac: WRITE" : "  - Thao tac: READ", 0x0E);
    vga_print((error_code & 0x4) ? "  - Ngu canh: User mode" : "  - Ngu canh: Kernel mode", 0x0E);

    uint64_t fault_addr = read_cr2();
    to_hex(buf, fault_addr);
    vga_print("  - Dia chi ao gay loi (CR2):", 0x0E);
    vga_print(buf, 0x0F);
}

} // namespace

// Điểm hội tụ mà MỌI stub trong isr_stubs.asm gọi vào.
// Đây chính là "lưới an toàn": thay vì triple fault câm lặng, giờ đây
// mọi exception đều in ra thông tin đọc được rồi dừng máy có kiểm soát.
extern "C" void isr_handler(Registers* regs) {
    char buf[19];

    vga_row = 0;
    vga_print("*** KERNEL EXCEPTION - he thong da dung lai ***", 0x4F);
    vga_print(exception_name(regs->vector), 0x0F);

    to_hex(buf, regs->vector);
    vga_print("Vector:", 0x07);
    vga_print(buf, 0x0F);

    to_hex(buf, regs->error_code);
    vga_print("Error code:", 0x07);
    vga_print(buf, 0x0F);

    to_hex(buf, regs->rip);
    vga_print("RIP (lenh gay loi):", 0x07);
    vga_print(buf, 0x0F);

    if (regs->vector == 14) {          // Page Fault
        print_page_fault_detail(regs->error_code);
    } else if (regs->vector == 13) {   // General Protection Fault
        vga_print("  - GPF: kiem tra lai selector/GDT hoac quyen truy cap", 0x0E);
    } else if (regs->vector == 8) {    // Double Fault
        vga_print("  - DOUBLE FAULT: fault xay ra ngay trong luc xu ly fault truoc", 0x0E);
        vga_print("  - Dang chay tren IST1 (stack rieng) nen an toan du stack chinh da hong", 0x0E);
    }

    // Dừng máy có kiểm soát: tắt interrupt rồi halt vĩnh viễn.
    // Đây là hành vi TẠM THỜI cho bước "lưới an toàn" — sau này với
    // page fault hợp lệ (demand paging), ta sẽ xử lý rồi return thay vì dừng.
    asm volatile("cli");
    for (;;) {
        asm volatile("hlt");
    }
}
