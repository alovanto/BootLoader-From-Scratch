#pragma once
#include <cstdint>
#include <cstddef>

// =========================================================================
// cpu.hpp — mọi thao tác chạm thẳng vào CPU: port I/O, MSR, control register,
// và vùng dữ liệu riêng của mỗi CPU (per-CPU).
//
// Mọi file khác PHẢI đi qua đây thay vì tự viết `asm volatile`, để chỉ có
// một chỗ duy nhất cần kiểm tra khi nghi ngờ cú pháp inline asm.
// =========================================================================

// ---------------------------------------------------------------- port I/O
inline uint8_t inb(uint16_t port) {
    uint8_t value;
    // AT&T syntax: nguồn trước, đích sau → `in %dx, %al`
    asm volatile("in %1, %0" : "=a"(value) : "d"(port));
    return value;
}

inline void outb(uint16_t port, uint8_t value) {
    asm volatile("out %1, %0" : : "d"(port), "a"(value));
}

// Ghi vào port 0x80 (POST code, không thiết bị nào dùng) để tạo độ trễ ~1µs.
// PIC 8259 đời cũ cần thời gian giữa hai lần ghi lệnh liên tiếp.
inline void io_wait() { outb(0x80, 0); }

// --------------------------------------------------------------------- MSR
constexpr uint32_t MSR_EFER           = 0xC0000080;
constexpr uint32_t MSR_GS_BASE        = 0xC0000101;
constexpr uint32_t MSR_KERNEL_GS_BASE = 0xC0000102;

inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

inline void wrmsr(uint32_t msr, uint64_t value) {
    asm volatile("wrmsr"
                 :
                 : "c"(msr),
                   "a"(static_cast<uint32_t>(value)),
                   "d"(static_cast<uint32_t>(value >> 32)));
}

// ---------------------------------------------------- control register & cờ
inline uint64_t read_cr0() { uint64_t v; asm volatile("mov %%cr0, %0" : "=r"(v)); return v; }
inline void     write_cr0(uint64_t v) { asm volatile("mov %0, %%cr0" : : "r"(v)); }
inline uint64_t read_cr2() { uint64_t v; asm volatile("mov %%cr2, %0" : "=r"(v)); return v; }
inline uint64_t read_cr3() { uint64_t v; asm volatile("mov %%cr3, %0" : "=r"(v)); return v; }
inline void     write_cr3(uint64_t v) { asm volatile("mov %0, %%cr3" : : "r"(v)); }

constexpr uint64_t RFLAGS_IF = 1ULL << 9;

inline uint64_t read_rflags() {
    uint64_t flags;
    asm volatile("pushfq; popq %0" : "=r"(flags));
    return flags;
}

inline void cpu_disable_interrupts() { asm volatile("cli"); }
inline void cpu_enable_interrupts()  { asm volatile("sti"); }
inline void cpu_halt()               { asm volatile("hlt"); }

// Tắt ngắt và trả về trạng thái cũ, để hàm gọi khôi phục đúng trạng thái đó
// thay vì `sti` vô điều kiện (sẽ bật ngắt ở nơi lẽ ra phải tắt).
inline uint64_t cpu_save_flags_and_cli() {
    const uint64_t flags = read_rflags();
    cpu_disable_interrupts();
    return flags;
}

inline void cpu_restore_flags(uint64_t flags) {
    if (flags & RFLAGS_IF) cpu_enable_interrupts();
}

// ------------------------------------------------------------------- CPUID
inline void cpuid(uint32_t leaf, uint32_t* eax, uint32_t* ebx, uint32_t* ecx, uint32_t* edx) {
    asm volatile("cpuid"
                 : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                 : "a"(leaf), "c"(0u));
}

// ----------------------------------------------------------------- per-CPU
// Vùng dữ liệu riêng của một CPU, tương đương KPCR/KPRCB của Windows. Thanh
// ghi GS trỏ vào đây khi CPU đang chạy code kernel, nên đọc `gs:[offset]` là
// cách lấy dữ liệu của ĐÚNG CPU hiện tại mà không cần biết mình là CPU số mấy
// (hiện mới có 1 CPU, nhưng cấu trúc này giữ nguyên khi thêm SMP).
//
// Offset của các field dưới đây sẽ bị hardcode trong assembly (syscall entry)
// ở bước user mode — static_assert bên dưới là hợp đồng giữa C++ và asm.
struct Cpu {
    Cpu*     self;        // gs:0  — this_cpu() đọc chính ô này để lấy con trỏ
    uint64_t kernel_rsp;  // gs:8  — đỉnh kernel stack của thread hiện tại
    uint64_t user_rsp;    // gs:16 — chỗ cất tạm RSP của user trong lúc vào syscall
    uint64_t ticks;       // gs:24 — số tick timer kể từ khi boot
};

static_assert(offsetof(Cpu, self)       == 0,  "syscall entry (asm) doc gs:0");
static_assert(offsetof(Cpu, kernel_rsp) == 8,  "syscall entry (asm) doc gs:8");
static_assert(offsetof(Cpu, user_rsp)   == 16, "syscall entry (asm) doc gs:16");

inline Cpu& this_cpu() {
    Cpu* cpu;
    asm volatile("mov %%gs:0, %0" : "=r"(cpu));
    return *cpu;
}

// Nạp GS_BASE để this_cpu() hoạt động. PHẢI gọi trước bất kỳ code nào dùng
// this_cpu(); trước lúc đó, đọc gs:0 sẽ lấy phải rác ở địa chỉ 0.
void cpu_init_percpu();
