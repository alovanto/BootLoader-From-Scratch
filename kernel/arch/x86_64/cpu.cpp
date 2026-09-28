#include "cpu.hpp"

namespace {

// Khởi tạo hằng (constant initialization): giá trị nằm sẵn trong .data của ảnh
// kernel, KHÔNG cần constructor chạy trước — nhờ vậy cpu_init_percpu() gọi được
// ở giai đoạn sớm nhất, trước cả run_global_constructors().
Cpu cpu0 = {&cpu0, 0, 0, 0};

}  // namespace

void cpu_init_percpu() {
    // GS_BASE       = vùng per-CPU của kernel.
    // KERNEL_GS_BASE = 0 (giá trị dành cho user mode).
    // Lệnh `swapgs` hoán đổi hai thanh ghi này mỗi khi vượt ranh giới ring —
    // chưa dùng tới khi còn chạy 100% ring 0, nhưng đặt đúng ngay từ đầu để
    // sau này thêm user mode không phải sửa lại chỗ này.
    wrmsr(MSR_GS_BASE, reinterpret_cast<uint64_t>(&cpu0));
    wrmsr(MSR_KERNEL_GS_BASE, 0);
}
