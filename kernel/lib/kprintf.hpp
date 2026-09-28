#pragma once
#include <cstdarg>
#include <cstdint>

// =========================================================================
// kprintf — hàm in dùng chung cho TOÀN BỘ kernel.
//
// Trước đây mỗi file tự viết lại to_hex()/vga_print() riêng
// (interrupt_handlers.cpp, mm/e820.cpp, mm/pmm.cpp đều có bản sao) — càng
// nhiều subsystem thì càng nhiều bản sao lệch nhau. Gom về một chỗ trước khi
// viết thêm VMM/heap/scheduler.
//
// Định dạng hỗ trợ (cố ý tối giản, không có float vì kernel build với
// -mgeneral-regs-only nên không được phép dùng SSE/x87):
//   %d  %ld   số có dấu             %u  %lu   số không dấu
//   %x  %lx   hex                   %p        con trỏ (luôn 16 chữ số)
//   %s        chuỗi                 %c        ký tự          %%  dấu %
//   Bổ nghĩa độ rộng: %8x, %08x, %16lx
// =========================================================================
void kprintf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void vkprintf(const char* fmt, va_list ap);

// Ghi thẳng ra cả hai "màn hình" (serial + VGA) — kprintf dùng bên trong, và
// panic dùng trực tiếp khi không tin tưởng trạng thái còn lại của hệ thống.
void console_putc(char c);
void console_puts(const char* s);
