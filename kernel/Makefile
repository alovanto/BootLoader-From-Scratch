CXX      = g++
AS       = nasm
LD       = ld

ARCH_DIR    = arch/x86_64
MM_DIR      = mm
LIB_DIR     = lib
DRIVERS_DIR = drivers

# Mức tối ưu: mặc định -O2. Build bản dễ debug bằng `make OPT=-O0`.
# Nên thử cả hai: -O2 làm lộ những lỗi mà -O0 che đi (thiếu volatile, UB...).
OPT ?= -O2

# Cờ freestanding bắt buộc cho code kernel. Lý do từng cờ:
#   -ffreestanding             : không giả định có libc/OS bên dưới
#   -fno-exceptions/-fno-rtti  : exception & RTTI cần runtime C++ phức tạp
#   -fno-threadsafe-statics    : biến static cục bộ có ctor sẽ gọi __cxa_guard_acquire
#   -fno-stack-protector       : Ubuntu/Debian bật mặc định -> cần __stack_chk_fail
#                                và canary ở %fs:0x28 (FS base = 0 trong kernel)
#   -fno-asynchronous-unwind-tables : không sinh .eh_frame vô dụng
#   -fcf-protection=none       : không chèn endbr64, giữ disassembly dễ đọc
#   -mno-red-zone              : BẮT BUỘC — exception bất đồng bộ sẽ đè lên red zone
#   -mcmodel=kernel -fno-pic -fno-pie : địa chỉ tuyệt đối, không position-independent
#   -mgeneral-regs-only        : cấm SSE/x87/MMX. ISR và context switch KHÔNG lưu
#                                thanh ghi XMM, nên chỉ cần g++ dùng XMM để copy
#                                một struct là dữ liệu hỏng âm thầm
#   -fno-omit-frame-pointer    : để panic() lần được chuỗi RBP mà in backtrace
CXXFLAGS = -std=c++17 -ffreestanding -fno-exceptions -fno-rtti \
           -fno-threadsafe-statics -fno-stack-protector \
           -fno-asynchronous-unwind-tables -fcf-protection=none \
           -mno-red-zone -mcmodel=kernel -fno-pic -fno-pie \
           -mgeneral-regs-only -fno-omit-frame-pointer \
           $(OPT) -g \
           -Wall -Wextra -Werror=return-type \
           -I. -I$(ARCH_DIR) \
           -MMD -MP -c

ASFLAGS  = -f elf64 -g -F dwarf

# KHÔNG link libgcc của host: nó được biên dịch với red zone và SSE.
LDFLAGS  = -m elf_x86_64 -T linker.ld -nostdlib -z noexecstack

# Object file của kernel_entry.o PHẢI đứng đầu danh sách OBJS — linker.ld
# gộp .text theo đúng thứ tự liệt kê ở đây, và _start phải nằm ở byte đầu
# tiên của kernel.bin để bootloader nhảy đúng chỗ (xem CLAUDE.md mục 6).
OBJS = kernel_entry.o \
       kernel_main.o \
       $(ARCH_DIR)/gdt.o \
       $(ARCH_DIR)/gdt_flush.o \
       $(ARCH_DIR)/tss.o \
       $(ARCH_DIR)/idt.o \
       $(ARCH_DIR)/interrupt_handlers.o \
       $(ARCH_DIR)/isr_stubs.o \
       $(ARCH_DIR)/pic.o \
       $(ARCH_DIR)/cpu.o \
       $(LIB_DIR)/string.o \
       $(LIB_DIR)/kprintf.o \
       $(LIB_DIR)/panic.o \
       $(LIB_DIR)/cxx_runtime.o \
       $(DRIVERS_DIR)/serial.o \
       $(DRIVERS_DIR)/vga_console.o \
       $(MM_DIR)/e820.o \
       $(MM_DIR)/pmm.o

# File .d do -MMD sinh ra: theo dõi phụ thuộc header tự động, thay cho việc
# liệt kê HEADERS bằng tay (kiểu cũ rất dễ quên cập nhật khi thêm header mới).
DEPS = $(OBJS:.o=.d)

all: kernel.bin

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

%.o: %.asm
	$(AS) $(ASFLAGS) $< -o $@

kernel.elf: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o kernel.elf $(OBJS)

# kernel.elf được giữ lại (không xoá) vì GDB cần nó để có symbol:
#   gdb kernel/kernel.elf -ex "target remote :1234"
kernel.bin: kernel.elf
	objcopy -O binary kernel.elf kernel.bin

clean:
	rm -f *.o *.d $(ARCH_DIR)/*.o $(ARCH_DIR)/*.d $(MM_DIR)/*.o $(MM_DIR)/*.d \
	      $(LIB_DIR)/*.o $(LIB_DIR)/*.d $(DRIVERS_DIR)/*.o $(DRIVERS_DIR)/*.d \
	      kernel.elf kernel.bin

-include $(DEPS)

.PHONY: all clean
