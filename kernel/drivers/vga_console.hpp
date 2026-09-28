#pragma once
#include <cstdint>

// Console VGA text mode 80x25 có cuộn màn hình.
//
// Khác với cách in cũ (mỗi nơi tự tính toạ độ dòng/cột rồi ghi thẳng vào
// 0xB8000): ở đây console tự giữ vị trí con trỏ, tự xuống dòng và tự cuộn khi
// đầy, nên các subsystem chỉ cần gọi kprintf mà không phải thoả thuận với nhau
// xem ai được dùng dòng nào.
void vga_console_init();
void vga_console_putc(char c);
void vga_console_set_color(uint8_t color);
uint8_t vga_console_color();

// Thuộc tính màu VGA: 4 bit thấp = màu chữ, 4 bit cao = màu nền
constexpr uint8_t VGA_COLOR_DEFAULT = 0x07;  // xám trên nền đen
constexpr uint8_t VGA_COLOR_INFO    = 0x0B;  // xanh lơ sáng
constexpr uint8_t VGA_COLOR_GOOD    = 0x0A;  // xanh lá sáng
constexpr uint8_t VGA_COLOR_WARN    = 0x0E;  // vàng
constexpr uint8_t VGA_COLOR_PANIC   = 0x4F;  // chữ trắng nền đỏ
