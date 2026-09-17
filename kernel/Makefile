CXX      = g++
AS       = nasm
LD       = ld

ARCH_DIR = arch/x86_64
MM_DIR   = mm

# Cờ freestanding bắt buộc cho code kernel:
#   -ffreestanding      : không giả định có libc/OS bên dưới
#   -fno-exceptions      : exception C++ cần runtime unwinding phức tạp, tắt đi
#   -fno-rtti            : bỏ RTTI (dynamic_cast/typeid) để giảm phụ thuộc runtime
#   -mno-red-zone        : bắt buộc cho code chạy trong ISR (đọc docs/notes/idt-isr-double-fault.md)
#   -mcmodel=kernel       : cho phép đặt code ở địa chỉ cao/thấp cố định như kernel thật
#   -fno-pic -fno-pie     : kernel không cần position-independent code
#   -I$(ARCH_DIR)         : cho phép kernel_main.cpp include "idt.hpp"/"gdt.hpp" dù
#                           header đang nằm trong arch/x86_64/, không phải cùng thư mục
CXXFLAGS = -std=c++17 -ffreestanding -fno-exceptions -fno-rtti \
           -mno-red-zone -mcmodel=kernel -fno-pic -fno-pie \
           -I$(ARCH_DIR) -Wall -Wextra -c

ASFLAGS  = -f elf64

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
       $(MM_DIR)/e820.o \
       $(MM_DIR)/pmm.o

HEADERS = $(ARCH_DIR)/idt.hpp $(ARCH_DIR)/gdt.hpp $(ARCH_DIR)/tss.hpp $(MM_DIR)/e820.hpp $(MM_DIR)/pmm.hpp

all: kernel.bin

%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) $< -o $@

%.o: %.asm
	$(AS) $(ASFLAGS) $< -o $@

kernel.elf: $(OBJS) linker.ld
	$(LD) -m elf_x86_64 -T linker.ld -o kernel.elf $(OBJS)

kernel.bin: kernel.elf
	objcopy -O binary kernel.elf kernel.bin

clean:
	rm -f *.o $(ARCH_DIR)/*.o $(MM_DIR)/*.o kernel.elf kernel.bin

.PHONY: all clean
