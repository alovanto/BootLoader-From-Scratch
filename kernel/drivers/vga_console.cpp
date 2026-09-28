#include "vga_console.hpp"
#include "cpu.hpp"

namespace {

constexpr int ROWS = 25;
constexpr int COLS = 80;

// Địa chỉ vật lý của VGA text buffer. Đang đọc/ghi thẳng bằng địa chỉ vật lý vì
// bootloader identity-map 1GB đầu. Khi VMM dựng bảng trang riêng và bỏ
// identity map, dòng này PHẢI đổi thành phys_to_virt(0xB8000).
volatile uint16_t* const VGA_BUFFER = reinterpret_cast<volatile uint16_t*>(0xB8000);

// Cổng điều khiển CRT controller — dùng để di chuyển con trỏ nhấp nháy của phần cứng
constexpr uint16_t CRTC_INDEX = 0x3D4;
constexpr uint16_t CRTC_DATA  = 0x3D5;

int row = 0;
int col = 0;
uint8_t current_color = VGA_COLOR_DEFAULT;

inline uint16_t make_cell(char c, uint8_t color) {
    // Mỗi ô 2 byte: byte thấp = mã ASCII, byte cao = thuộc tính màu
    return static_cast<uint16_t>(color) << 8 | static_cast<uint8_t>(c);
}

void clear_row(int r) {
    for (int c = 0; c < COLS; c++) {
        VGA_BUFFER[r * COLS + c] = make_cell(' ', current_color);
    }
}

void scroll_up() {
    for (int r = 1; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            VGA_BUFFER[(r - 1) * COLS + c] = VGA_BUFFER[r * COLS + c];
        }
    }
    clear_row(ROWS - 1);
}

void update_hardware_cursor() {
    const uint16_t position = static_cast<uint16_t>(row * COLS + col);
    outb(CRTC_INDEX, 0x0F);                                        // thanh ghi 0x0F: byte thấp vị trí con trỏ
    outb(CRTC_DATA, static_cast<uint8_t>(position & 0xFF));
    outb(CRTC_INDEX, 0x0E);                                        // thanh ghi 0x0E: byte cao
    outb(CRTC_DATA, static_cast<uint8_t>((position >> 8) & 0xFF));
}

}  // namespace

void vga_console_init() {
    row = 0;
    col = 0;
    current_color = VGA_COLOR_DEFAULT;
    for (int r = 0; r < ROWS; r++) {
        clear_row(r);
    }
    update_hardware_cursor();
}

void vga_console_set_color(uint8_t color) { current_color = color; }
uint8_t vga_console_color() { return current_color; }

void vga_console_putc(char c) {
    if (c == '\n') {
        col = 0;
        row++;
    } else if (c == '\r') {
        col = 0;
    } else if (c == '\t') {
        col = (col + 8) & ~7;
    } else if (c == '\b') {
        if (col > 0) {
            col--;
            VGA_BUFFER[row * COLS + col] = make_cell(' ', current_color);
        }
    } else {
        VGA_BUFFER[row * COLS + col] = make_cell(c, current_color);
        col++;
    }

    if (col >= COLS) {
        col = 0;
        row++;
    }
    if (row >= ROWS) {
        scroll_up();
        row = ROWS - 1;
    }
    update_hardware_cursor();
}
