#include "idt.hpp"
#include "pic.hpp"
#include "cpu.hpp"
#include "lib/kprintf.hpp"
#include "lib/panic.hpp"

namespace {

const char* exception_name(uint64_t vector) {
    static const char* const names[32] = {
        "Divide Error (#DE)", "Debug (#DB)", "NMI", "Breakpoint (#BP)",
        "Overflow (#OF)", "BOUND Range (#BR)", "Invalid Opcode (#UD)", "Device Not Available (#NM)",
        "Double Fault (#DF)", "Coprocessor Overrun", "Invalid TSS (#TS)", "Segment Not Present (#NP)",
        "Stack Fault (#SS)", "General Protection Fault (#GP)", "Page Fault (#PF)", "Reserved",
        "x87 FP Error (#MF)", "Alignment Check (#AC)", "Machine Check (#MC)", "SIMD FP (#XM)",
        "Virtualization (#VE)", "Control Protection (#CP)", "Reserved", "Reserved",
        "Reserved", "Reserved", "Reserved", "Reserved",
        "Reserved", "Reserved", "Security Exception (#SX)", "Reserved"
    };
    return vector < 32 ? names[vector] : "Unknown";
}

// Giải mã error code của Page Fault (Intel SDM Vol.3 §4.7). Đây là thông tin
// quyết định: "chưa map" và "map rồi nhưng sai quyền" là hai loại bug khác hẳn nhau.
void describe_page_fault(uint64_t error_code) {
    const uint64_t fault_address = read_cr2();
    kprintf("Page Fault tai dia chi ao %p\n", reinterpret_cast<void*>(fault_address));
    kprintf("  - %s\n", (error_code & 0x1) ? "Trang CO ton tai -> vi pham quyen truy cap"
                                           : "Trang KHONG ton tai (chua duoc map)");
    kprintf("  - Thao tac: %s\n", (error_code & 0x2) ? "GHI" : "DOC");
    kprintf("  - Ngu canh: %s\n", (error_code & 0x4) ? "user mode (ring 3)" : "kernel mode (ring 0)");
    if (error_code & 0x8)  kprintf("  - Bit reserved trong page table bi set -> bang trang hong\n");
    if (error_code & 0x10) kprintf("  - Loi khi NAP LENH (thuc thi vung khong cho phep)\n");
}

void handle_exception(InterruptFrame* frame) {
    kprintf("\n--- CPU EXCEPTION: %s (vector %lu) ---\n",
            exception_name(frame->vector), frame->vector);

    switch (frame->vector) {
    case 14:
        describe_page_fault(frame->error_code);
        break;
    case 13:
        // Error code của #GP, nếu khác 0, chính là selector gây lỗi
        kprintf("GPF: selector lien quan = 0x%lx (0 = khong lien quan toi segment)\n",
                frame->error_code);
        break;
    case VECTOR_DOUBLE_FAULT:
        kprintf("Double Fault: mot loi xay ra NGAY TRONG luc xu ly loi truoc do.\n");
        kprintf("Handler nay dang chay tren stack rieng IST1, nen van an toan du\n");
        kprintf("stack chinh da hong. Kiem tra RSP ben duoi: neu no nam ngay duoi\n");
        kprintf("day vung kernel stack thi nguyen nhan la TRAN STACK.\n");
        break;
    default:
        break;
    }

    // Mọi exception hiện đều là bug của kernel (chưa có user mode để đổ lỗi, và
    // chưa có VMM để xử lý page fault hợp lệ kiểu demand paging).
    panic_frame(frame, "exception khong xu ly duoc: %s", exception_name(frame->vector));
}

}  // namespace

// Điểm hội tụ mà MỌI stub trong isr_stubs.asm gọi vào.
extern "C" void interrupt_dispatch(InterruptFrame* frame) {
    if (frame->vector < 32) {
        handle_exception(frame);   // không bao giờ return (panic)
        return;
    }

    if (frame->vector < PIC_VECTOR_BASE + 16) {
        const uint8_t irq = static_cast<uint8_t>(frame->vector - PIC_VECTOR_BASE);

        // Ngắt ma thì không được gửi EOI, nếu không sẽ xác nhận nhầm một ngắt
        // khác đang thực sự chờ xử lý.
        if ((irq == 7 || irq == 15) && pic_is_spurious(irq)) {
            if (irq == 15) pic_send_eoi(2);   // slave giả nhưng master vẫn thấy IRQ2 thật
            return;
        }

        // Chưa subsystem nào đăng ký IRQ và toàn bộ IRQ đang bị mask, nên tới
        // được đây là bất thường — báo rõ thay vì im lặng.
        kprintf("[irq] IRQ%u bat ngo (vector %lu) - chua co driver dang ky\n",
                static_cast<unsigned>(irq), frame->vector);
        pic_send_eoi(irq);
        return;
    }

    kprintf("[irq] vector %lu khong mong doi, bo qua\n", frame->vector);
}
