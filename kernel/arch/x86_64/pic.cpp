#include "pic.hpp"
#include "cpu.hpp"

namespace {

// Hai chip 8259 mắc nối tiếp (cascade): slave nối vào chân IRQ2 của master.
constexpr uint16_t PIC1_COMMAND = 0x20;
constexpr uint16_t PIC1_DATA    = 0x21;
constexpr uint16_t PIC2_COMMAND = 0xA0;
constexpr uint16_t PIC2_DATA    = 0xA1;

constexpr uint8_t ICW1_INIT     = 0x10;  // bắt đầu chuỗi khởi tạo
constexpr uint8_t ICW1_ICW4     = 0x01;  // sẽ gửi cả ICW4
constexpr uint8_t ICW4_8086     = 0x01;  // chế độ 8086/88 (thay vì MCS-80/85)
constexpr uint8_t OCW3_READ_ISR = 0x0B;  // lệnh yêu cầu đọc In-Service Register
constexpr uint8_t PIC_EOI       = 0x20;

constexpr uint8_t SLAVE_IRQ_LINE = 2;

}  // namespace

void pic_remap_and_mask_all() {
    // Chuỗi ICW1→ICW4 phải gửi liên tiếp, đúng thứ tự, cho cả hai chip.
    // io_wait() giữa các lần ghi vì PIC đời cũ cần thời gian xử lý.
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);  io_wait();
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);  io_wait();

    outb(PIC1_DATA, PIC_VECTOR_BASE);           io_wait();  // ICW2: IRQ0-7  → vector 32-39
    outb(PIC2_DATA, PIC_VECTOR_BASE + 8);       io_wait();  //       IRQ8-15 → vector 40-47

    outb(PIC1_DATA, 1u << SLAVE_IRQ_LINE);      io_wait();  // ICW3 (master): bitmask chân có slave
    outb(PIC2_DATA, SLAVE_IRQ_LINE);            io_wait();  // ICW3 (slave): số hiệu chân mình đang nối

    outb(PIC1_DATA, ICW4_8086);                 io_wait();
    outb(PIC2_DATA, ICW4_8086);                 io_wait();

    // Mask toàn bộ: chưa driver nào sẵn sàng nhận ngắt. Từng driver sẽ tự mở
    // đúng IRQ của mình khi đăng ký handler.
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

void pic_mask(uint8_t irq) {
    const uint16_t port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    const uint8_t bit = irq < 8 ? irq : static_cast<uint8_t>(irq - 8);
    outb(port, static_cast<uint8_t>(inb(port) | (1u << bit)));
}

void pic_unmask(uint8_t irq) {
    const uint16_t port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    const uint8_t bit = irq < 8 ? irq : static_cast<uint8_t>(irq - 8);
    outb(port, static_cast<uint8_t>(inb(port) & ~(1u << bit)));
    if (irq >= 8) {
        // IRQ của slave chỉ tới được CPU nếu chân cascade trên master cũng mở
        outb(PIC1_DATA, static_cast<uint8_t>(inb(PIC1_DATA) & ~(1u << SLAVE_IRQ_LINE)));
    }
}

void pic_send_eoi(uint8_t irq) {
    // IRQ của slave phải báo cho CẢ HAI chip: slave trước, rồi master (vì với
    // master thì ngắt đó đến từ chân cascade IRQ2).
    if (irq >= 8) outb(PIC2_COMMAND, PIC_EOI);
    outb(PIC1_COMMAND, PIC_EOI);
}

bool pic_is_spurious(uint8_t irq) {
    const uint16_t command_port = irq < 8 ? PIC1_COMMAND : PIC2_COMMAND;
    outb(command_port, OCW3_READ_ISR);
    // Bit 7 của ISR = 1 nghĩa là IRQ7/IRQ15 đang thực sự được phục vụ.
    // Bằng 0 nghĩa là ngắt ma.
    return (inb(command_port) & 0x80) == 0;
}
