#include "gdt.hpp"
#include "idt.hpp"
#include "pic.hpp"
#include "cpu.hpp"
#include "drivers/serial.hpp"
#include "drivers/vga_console.hpp"
#include "lib/init.hpp"
#include "lib/kprintf.hpp"
#include "lib/panic.hpp"
#include "mm/e820.hpp"
#include "mm/pmm.hpp"

namespace {

// Chứng minh allocator hoạt động đúng: frame đã trả lại phải được tái sử dụng,
// chứ không rò rỉ. Đây là bước "thấy kết quả trên QEMU" bắt buộc theo CLAUDE.md
// mục 10 — build thành công không chứng minh được logic đúng.
void demo_frame_allocator() {
    const uint64_t frame_a = pmm_alloc_frame();
    const uint64_t frame_b = pmm_alloc_frame();
    kprintf("  cap frame A = %p\n", reinterpret_cast<void*>(frame_a));
    kprintf("  cap frame B = %p\n", reinterpret_cast<void*>(frame_b));

    pmm_free_frame(frame_a);
    const uint64_t frame_c = pmm_alloc_frame();
    kprintf("  tra lai A, cap tiep    = %p  -> %s\n",
            reinterpret_cast<void*>(frame_c),
            frame_c == frame_a ? "DUNG (tai su dung dung frame vua tra)"
                               : "SAI (frame vua tra khong duoc dung lai)");

    pmm_free_frame(frame_b);
    pmm_free_frame(frame_c);
}

}  // namespace

// extern "C": bắt buộc để tên symbol không bị C++ name-mangling —
// nhờ vậy kernel_entry.asm gọi được bằng đúng tên "kernel_main".
extern "C" void kernel_main() {
    // --- Giai đoạn 1: làm cho kernel "nói" được, trước mọi thứ khác ---
    // Không có kênh log thì mọi lỗi ở các bước sau đều biểu hiện giống nhau:
    // máy đứng im, không manh mối.
    serial_init();
    vga_console_init();
    kprintf("\n=== my-os ===\n");
    kprintf("[boot] serial COM1 san sang (QEMU: chay voi -serial stdio)\n");

    // Constructor của object toàn cục. Đặt sau serial để nếu có constructor
    // nào panic thì vẫn thấy được thông báo.
    run_global_constructors();

    // --- Giai đoạn 2: dựng "lưới an toàn" của CPU ---
    gdt_init();         // GDT riêng của kernel + TSS (3 stack IST)
    cpu_init_percpu();  // GS_BASE -> vùng dữ liệu per-CPU
    kprintf("[cpu ] GDT + TSS + per-CPU da san sang\n");

    idt_init();         // 256 vector; IST1/2/3 cho #DF, NMI, #MC
    kprintf("[idt ] 256 vector da dang ky (IST rieng cho #DF, NMI, #MC)\n");

    // PIC mặc định ánh xạ IRQ0-7 đè lên vector 8-15 — trùng #DF, #GP, #PF.
    // Remap sang 32-47 và mask hết. Ngắt vẫn đang TẮT (IF=0) và chỉ được bật
    // khi có scheduler; nhưng PIC phải đúng từ bây giờ để không còn khả năng
    // một tick timer bị hiểu nhầm thành Double Fault.
    pic_remap_and_mask_all();
    kprintf("[pic ] da remap IRQ0-15 -> vector 32-47 va mask toan bo\n");

    // --- Giai đoạn 3: bộ nhớ vật lý ---
    kprintf("\n[mm  ] ban do RAM tu BIOS (E820):\n");
    e820_dump();

    pmm_init();
    kprintf("[mm  ] Physical Frame Allocator:\n");
    pmm_dump();
    demo_frame_allocator();

    kprintf("\n[boot] khoi dong xong. Buoc tiep theo: Virtual Memory Manager.\n");
    kprintf("[boot] CPU dung lai (ngat van TAT cho toi khi co scheduler).\n");

    // `hlt` với IF=0 nghĩa là dừng hẳn — đúng ý đồ ở giai đoạn này: chưa có
    // scheduler thì cũng chưa có việc gì để làm khi bị đánh thức.
    for (;;) {
        cpu_halt();
    }
}
