# Makefile top-level — ghép os.img từ boot/boot.asm + kernel/kernel.bin,
# và cung cấp target run/debug để test trên QEMU (xem CLAUDE.md mục 10).

AS    = nasm
QEMU  = qemu-system-x86_64

BOOT_SRC = boot/boot.asm
BOOT_BIN = boot/boot.bin

KERNEL_DIR = kernel
KERNEL_BIN = kernel/kernel.bin

IMG = os.img

# PHẢI khớp KERNEL_SECTORS trong boot/boot.asm (xem CLAUDE.md mục 6) —
# đây là số sector Stage2 sẽ đọc từ đĩa cho kernel.bin. Nếu kernel.bin build
# ra nhỏ hơn ngưỡng này, ta đệm (pad) thêm 0 cho đủ; đĩa ảo phải có đúng số
# byte này ngay sau phần bootloader để LBA đọc không bị lệch.
KERNEL_SECTORS = 128
KERNEL_PADDED_SIZE = $(shell echo $$(( $(KERNEL_SECTORS) * 512 )))

.PHONY: all run debug clean

all: $(IMG)

$(BOOT_BIN): $(BOOT_SRC)
	$(AS) -f bin -o $@ $<

# kernel.bin phụ thuộc và luôn build lại qua sub-make (kernel/Makefile tự lo
# việc theo dõi phụ thuộc chi tiết giữa các file .cpp/.asm/.hpp của nó).
.PHONY: $(KERNEL_BIN)
$(KERNEL_BIN):
	$(MAKE) -C $(KERNEL_DIR)

$(IMG): $(BOOT_BIN) $(KERNEL_BIN)
	cp $(KERNEL_BIN) kernel.padded.bin
	truncate -s $(KERNEL_PADDED_SIZE) kernel.padded.bin
	cat $(BOOT_BIN) kernel.padded.bin > $(IMG)
	rm -f kernel.padded.bin

run: $(IMG)
	$(QEMU) -drive format=raw,file=$(IMG)

# Chạy kèm log exception/CPU reset — dùng khi nghi ngờ triple fault
# (xem CLAUDE.md mục 10).
debug: $(IMG)
	$(QEMU) -drive format=raw,file=$(IMG) -d int,cpu_reset -no-reboot -no-shutdown

clean:
	$(MAKE) -C $(KERNEL_DIR) clean
	rm -f $(BOOT_BIN) $(IMG) kernel.padded.bin
