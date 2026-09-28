#include "lib/panic.hpp"
#include "lib/kprintf.hpp"
#include "cpu.hpp"
#include "idt.hpp"

namespace {

// Đang panic rồi thì không quan tâm khoá hay trạng thái nữa — cờ này chỉ để
// chặn đệ quy vô hạn nếu chính đường in ấn lại panic tiếp.
bool panicking = false;

// Khoảng địa chỉ hợp lệ để đi theo chuỗi RBP. Kernel nằm ở 1MB và stack của nó
// nằm trong .bss ngay sau đó, nên 1MB–16MB bao trọn cả hai. Khi kernel chuyển
// lên higher-half, đổi thành [KERNEL_VMA, ...) và vùng kernel stack.
constexpr uint64_t FRAME_MIN = 0x100000;
constexpr uint64_t FRAME_MAX = 0x1000000;

[[noreturn]] void halt_forever() {
    cpu_disable_interrupts();
    for (;;) {
        cpu_halt();
    }
}

}  // namespace

void backtrace(uint64_t rbp) {
    kprintf("Backtrace (dia chi lenh goi, moi nhat truoc):\n");
    for (int depth = 0; depth < 24; depth++) {
        // Khung ngăn xếp chuẩn: [rbp] = rbp của hàm gọi, [rbp+8] = địa chỉ trả về.
        // Kiểm tra kỹ trước khi dereference: đang panic nên rất có thể stack đã hỏng,
        // một page fault ở đây sẽ nuốt mất toàn bộ chẩn đoán.
        if (rbp < FRAME_MIN || rbp >= FRAME_MAX || (rbp & 7) != 0) break;
        const uint64_t* frame = reinterpret_cast<const uint64_t*>(rbp);
        const uint64_t return_address = frame[1];
        if (return_address == 0) break;
        kprintf("  #%d  %p\n", depth, reinterpret_cast<void*>(return_address));
        rbp = frame[0];
    }
    kprintf("  (giai ma ten ham: addr2line -f -C -e kernel/kernel.elf <dia chi>)\n");
}

[[noreturn]] void panic(const char* fmt, ...) {
    cpu_disable_interrupts();
    const bool nested = panicking;
    panicking = true;

    kprintf("\n=== KERNEL PANIC ===\n");
    va_list ap;
    va_start(ap, fmt);
    vkprintf(fmt, ap);
    va_end(ap);
    kprintf("\n");

    if (!nested) {
        uint64_t rbp;
        asm volatile("mov %%rbp, %0" : "=r"(rbp));
        backtrace(rbp);
    }
    kprintf("He thong da dung lai.\n");
    halt_forever();
}

[[noreturn]] void panic_frame(const InterruptFrame* frame, const char* fmt, ...) {
    cpu_disable_interrupts();
    const bool nested = panicking;
    panicking = true;

    kprintf("\n=== KERNEL PANIC ===\n");
    va_list ap;
    va_start(ap, fmt);
    vkprintf(fmt, ap);
    va_end(ap);
    kprintf("\n");

    kprintf("vector=%lu  error_code=0x%lx\n", frame->vector, frame->error_code);
    kprintf("RIP=%p  CS=0x%lx  RFLAGS=0x%lx\n",
            reinterpret_cast<void*>(frame->rip), frame->cs, frame->rflags);
    // RSP/SS luôn có trong frame ở Long Mode, kể cả khi ngắt xảy ra ở ring 0 →
    // ring 0 (Intel SDM Vol.3 §6.14.2). Đây là thông tin then chốt để nhận ra
    // tràn kernel stack: RSP sẽ nằm ngay dưới đáy vùng stack.
    kprintf("RSP=%p  SS=0x%lx\n", reinterpret_cast<void*>(frame->rsp), frame->ss);
    kprintf("CR2=%p  CR3=%p\n",
            reinterpret_cast<void*>(read_cr2()), reinterpret_cast<void*>(read_cr3()));
    kprintf("RAX=%p RBX=%p RCX=%p RDX=%p\n",
            reinterpret_cast<void*>(frame->rax), reinterpret_cast<void*>(frame->rbx),
            reinterpret_cast<void*>(frame->rcx), reinterpret_cast<void*>(frame->rdx));
    kprintf("RSI=%p RDI=%p RBP=%p R8 =%p\n",
            reinterpret_cast<void*>(frame->rsi), reinterpret_cast<void*>(frame->rdi),
            reinterpret_cast<void*>(frame->rbp), reinterpret_cast<void*>(frame->r8));
    kprintf("R9 =%p R10=%p R11=%p R12=%p\n",
            reinterpret_cast<void*>(frame->r9), reinterpret_cast<void*>(frame->r10),
            reinterpret_cast<void*>(frame->r11), reinterpret_cast<void*>(frame->r12));
    kprintf("R13=%p R14=%p R15=%p\n",
            reinterpret_cast<void*>(frame->r13), reinterpret_cast<void*>(frame->r14),
            reinterpret_cast<void*>(frame->r15));

    if (!nested) backtrace(frame->rbp);
    kprintf("He thong da dung lai.\n");
    halt_forever();
}
