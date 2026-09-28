#include "serial.hpp"
#include "cpu.hpp"

namespace {

constexpr uint16_t COM1 = 0x3F8;

// Offset các thanh ghi của UART 16550 tính từ port gốc
constexpr uint16_t REG_DATA          = 0;  // đọc/ghi dữ liệu (khi DLAB=0)
constexpr uint16_t REG_INT_ENABLE    = 1;  // bật/tắt ngắt (khi DLAB=0)
constexpr uint16_t REG_DIVISOR_LOW   = 0;  // byte thấp của divisor (khi DLAB=1)
constexpr uint16_t REG_DIVISOR_HIGH  = 1;  // byte cao của divisor  (khi DLAB=1)
constexpr uint16_t REG_FIFO_CONTROL  = 2;
constexpr uint16_t REG_LINE_CONTROL  = 3;
constexpr uint16_t REG_MODEM_CONTROL = 4;
constexpr uint16_t REG_LINE_STATUS   = 5;

constexpr uint8_t LINE_STATUS_TX_EMPTY = 0x20;  // bit 5: thanh ghi truyền đã trống

}  // namespace

void serial_init() {
    outb(COM1 + REG_INT_ENABLE, 0x00);     // tắt mọi ngắt của UART (đang dùng polling)
    outb(COM1 + REG_LINE_CONTROL, 0x80);   // DLAB=1: hai port đầu tạm thành thanh ghi divisor
    outb(COM1 + REG_DIVISOR_LOW, 0x01);    // divisor = 1 → 115200 baud (115200 / 1)
    outb(COM1 + REG_DIVISOR_HIGH, 0x00);
    outb(COM1 + REG_LINE_CONTROL, 0x03);   // DLAB=0, khung 8 bit dữ liệu, không parity, 1 stop bit
    outb(COM1 + REG_FIFO_CONTROL, 0xC7);   // bật FIFO, xoá cả hai chiều, ngưỡng 14 byte
    outb(COM1 + REG_MODEM_CONTROL, 0x0B);  // DTR + RTS + OUT2
}

void serial_putc(char c) {
    // Chờ tới khi UART sẵn sàng nhận byte tiếp theo. Vòng lặp bận này chấp nhận
    // được vì (a) chưa có scheduler để nhường CPU, (b) ở 115200 baud thì mỗi
    // byte chỉ mất ~87µs, (c) log phải ra được ngay cả khi kernel đang panic.
    while ((inb(COM1 + REG_LINE_STATUS) & LINE_STATUS_TX_EMPTY) == 0) {
    }
    outb(COM1 + REG_DATA, static_cast<uint8_t>(c));
}
