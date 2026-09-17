#include "idt.hpp"
#include "gdt.hpp"
#include "mm/e820.hpp"
#include "mm/pmm.hpp"

namespace {
volatile uint16_t* const VGA = reinterpret_cast<volatile uint16_t*>(0xB8000);

void status_line(int row, const char* str, uint8_t color) {
    int col = 0;
    while (*str) {
        VGA[row * 80 + col] = static_cast<uint16_t>(color) << 8 | static_cast<uint8_t>(*str);
        str++;
        col++;
    }
}

void to_hex(char* out, uint64_t value) {
    const char* digits = "0123456789ABCDEF";
    out[0] = '0';
    out[1] = 'x';
    for (int i = 0; i < 16; i++) {
        out[2 + i] = digits[(value >> ((15 - i) * 4)) & 0xF];
    }
    out[18] = '\0';
}
}

// extern "C": bắt buộc để tên symbol không bị C++ name-mangling —
// nhờ vậy kernel_entry.asm gọi được bằng đúng tên "kernel_main".
extern "C" void kernel_main() {
    status_line(0, "Kernel: dang dung GDT + TSS rieng...", 0x0A);
    gdt_init();   // PHẢI chạy trước idt_init(): IDT gate tham chiếu selector
                  // trong GDT này, và IST=1 của Double Fault cần TSS đã active.

    status_line(1, "Kernel: IDT dang khoi tao...", 0x0A);
    idt_init();

    status_line(2, "Kernel: IDT san sang (co IST cho Double Fault). Luoi an toan da bat.", 0x0A);
    status_line(3, "Kernel: dang doc E820 memory map...", 0x0E);

    // Bản đồ RAM thật đã được boot.asm dò qua BIOS INT 15h/E820 ở Real Mode
    // (xem detect_memory_e820 trong boot/boot.asm) và ghi sẵn vào vật lý
    // 0x20000. In ra đây để verify bằng mắt trên QEMU — đúng nguyên tắc
    // "mỗi bước phải thấy kết quả trên QEMU" ở CLAUDE.md — trước khi
    // Physical Frame Allocator dùng nó làm input thật ngay bên dưới.
    e820_dump();

    // Physical Frame Allocator: dựng bitmap frame 4KB từ đúng bản đồ E820
    // vừa đọc. In dòng 18 (dưới danh sách E820, QEMU thường trả 6-10 entry
    // nên bắt đầu từ row 4 không lấn quá row 17).
    status_line(18, "Kernel: khoi tao Physical Frame Allocator...", 0x0E);
    pmm_init();
    pmm_dump(19);

    // --- DEMO: cấp 2 frame, free 1, cấp lại — chứng minh frame vừa free()
    // được TÁI SỬ DỤNG đúng (không rò rỉ), giống cách demo Page Fault trước
    // đây từng chứng minh ISR hoạt động (đã gỡ khi hoàn thành nhiệm vụ). ---
    char buf[19];
    const uint64_t frame_a = pmm_alloc_frame();
    const uint64_t frame_b = pmm_alloc_frame();

    to_hex(buf, frame_a);
    status_line(20, "  Frame A cap duoc:", 0x07);
    {
        int col = 20;
        for (char* c = buf; *c; c++) VGA[20 * 80 + col++] = static_cast<uint16_t>(0x0F) << 8 | static_cast<uint8_t>(*c);
    }

    to_hex(buf, frame_b);
    status_line(21, "  Frame B cap duoc:", 0x07);
    {
        int col = 20;
        for (char* c = buf; *c; c++) VGA[21 * 80 + col++] = static_cast<uint16_t>(0x0F) << 8 | static_cast<uint8_t>(*c);
    }

    pmm_free_frame(frame_a); // trả lại Frame A

    const uint64_t frame_c = pmm_alloc_frame(); // xin frame mới — kỳ vọng == frame_a
    to_hex(buf, frame_c);
    status_line(22, "  Frame C cap lai (ky vong = Frame A):", 0x07);
    {
        int col = 40;
        for (char* c = buf; *c; c++) VGA[22 * 80 + col++] = static_cast<uint16_t>(0x0F) << 8 | static_cast<uint8_t>(*c);
    }

    // Idle bình thường — CHƯA có Virtual Memory Manager/Scheduler nên chưa
    // có việc gì khác để làm. `hlt` để tiết kiệm CPU thay vì busy-loop.
    for (;;) {
        asm volatile("hlt");
    }
}
