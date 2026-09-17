#include "e820.hpp"

namespace {

volatile uint16_t* const VGA = reinterpret_cast<volatile uint16_t*>(0xB8000);
constexpr int VGA_COLS = 80;

// entry_count nằm ở 4 byte đầu buffer, mảng entry bắt đầu ngay sau đó —
// đúng layout mà detect_memory_e820 trong boot/boot.asm đã ghi.
uint32_t* const raw_entry_count = reinterpret_cast<uint32_t*>(E820_MAP_PHYS_ADDR);
E820Entry* const raw_entries    = reinterpret_cast<E820Entry*>(E820_MAP_PHYS_ADDR + 4);

void vga_print_at(int row, const char* str, uint8_t color) {
    int col = 0;
    while (*str && col < VGA_COLS) {
        VGA[row * VGA_COLS + col] = static_cast<uint16_t>(color) << 8 | static_cast<uint8_t>(*str);
        str++;
        col++;
    }
}

// Chuyển 1 số 64-bit thành chuỗi hex, không dùng libc (chưa có heap ở bước
// này — giống cách interrupt_handlers.cpp tự viết to_hex() riêng).
void to_hex(char* out, uint64_t value) {
    const char* digits = "0123456789ABCDEF";
    out[0] = '0';
    out[1] = 'x';
    for (int i = 0; i < 16; i++) {
        out[2 + i] = digits[(value >> ((15 - i) * 4)) & 0xF];
    }
    out[18] = '\0';
}

const char* type_name(uint32_t type) {
    switch (static_cast<E820Type>(type)) {
        case E820Type::Usable:          return "Usable";
        case E820Type::Reserved:        return "Reserved";
        case E820Type::AcpiReclaimable: return "ACPI Reclaimable";
        case E820Type::AcpiNvs:         return "ACPI NVS";
        case E820Type::Bad:             return "Bad";
        default:                        return "Unknown";
    }
}

// Nối chuỗi thủ công vào buffer dòng, trả về vị trí kế tiếp — chưa có
// libc/snprintf ở bước này nên phải tự ghép tay.
int append(char* dst, int pos, const char* src) {
    while (*src) {
        dst[pos++] = *src++;
    }
    return pos;
}

} // namespace

uint32_t e820_entry_count() {
    return *raw_entry_count;
}

const E820Entry* e820_entry(uint32_t index) {
    return &raw_entries[index];
}

void e820_dump() {
    // Row 0-3 đã bị kernel_main.cpp dùng cho log khởi động GDT/IDT, nên bắt
    // đầu in từ row 4 để không đè lên nhau.
    int row = 4;

    vga_print_at(row++, "E820 memory map:", 0x0B);

    const uint32_t count = e820_entry_count();
    if (count == 0) {
        vga_print_at(row, "  (khong co entry nao - BIOS/QEMU khong ho tro E820?)", 0x0C);
        return;
    }

    for (uint32_t i = 0; i < count; i++) {
        const E820Entry* e = e820_entry(i);

        char line[96];
        char hexbuf[19];
        int pos = 0;

        pos = append(line, pos, "  base=");
        to_hex(hexbuf, e->base);
        pos = append(line, pos, hexbuf);

        pos = append(line, pos, " len=");
        to_hex(hexbuf, e->length);
        pos = append(line, pos, hexbuf);

        pos = append(line, pos, " type=");
        pos = append(line, pos, type_name(e->type));
        line[pos] = '\0';

        // Xanh lá = Usable (vùng PFA sắp cấp phát từ đây), trắng = mọi loại khác
        const uint8_t color = (e->type == static_cast<uint32_t>(E820Type::Usable)) ? 0x0A : 0x07;
        vga_print_at(row++, line, color);
    }
}
