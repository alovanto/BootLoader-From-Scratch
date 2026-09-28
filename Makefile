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
#
# GIỚI HẠN 64KB không chỉ do con số này: kernel được nạp tạm vào 0x10000 và
# bảng E820 nằm ngay tại 0x20000, nên 128 sector (= 64KB) vừa đúng chạm mép.
# Muốn kernel lớn hơn thì phải đổi cách nạp (xem docs/architecture.md mục 5.1.2),
# không chỉ tăng con số ở hai nơi.
KERNEL_SECTORS = 128
KERNEL_PADDED_SIZE = $(shell echo $$(( $(KERNEL_SECTORS) * 512 )))

# Cấu hình QEMU dùng chung. `-serial stdio` đưa log kernel thẳng ra terminal
# này — kênh debug chính, thay cho việc đọc chữ trên màn hình VGA.
QEMU_FLAGS = -m 256M -drive format=raw,file=$(IMG) -serial stdio -no-reboot

.PHONY: all run debug gdb clean

all: $(IMG)

$(BOOT_BIN): $(BOOT_SRC)
	$(AS) -f bin -o $@ $<

# kernel.bin phụ thuộc và luôn build lại qua sub-make (kernel/Makefile tự lo
# việc theo dõi phụ thuộc chi tiết giữa các file .cpp/.asm/.hpp của nó).
.PHONY: $(KERNEL_BIN)
$(KERNEL_BIN):
	$(MAKE) -C $(KERNEL_DIR)

$(IMG): $(BOOT_BIN) $(KERNEL_BIN)
	@size=$$(stat -c %s $(KERNEL_BIN)); \
	limit=$(KERNEL_PADDED_SIZE); \
	if [ $$size -gt $$limit ]; then \
	  echo ""; \
	  echo "LOI: kernel.bin = $$size byte, vuot gioi han $$limit byte ($(KERNEL_SECTORS) sector)."; \
	  echo "     Truoc day buoc nay dung 'truncate' nen se CAT CUT kernel ma khong bao loi —"; \
	  echo "     kernel chay sai hoan toan khong ro ly do. Gio thi dung han o day."; \
	  echo "     Cach xu ly: xem docs/architecture.md muc 5.1.2 (nap kernel theo khoi)."; \
	  echo ""; \
	  exit 1; \
	fi; \
	echo "kernel.bin: $$size / $$limit byte da dung"
	cp $(KERNEL_BIN) kernel.padded.bin
	truncate -s $(KERNEL_PADDED_SIZE) kernel.padded.bin
	cat $(BOOT_BIN) kernel.padded.bin > $(IMG)
	rm -f kernel.padded.bin

run: $(IMG)
	$(QEMU) $(QEMU_FLAGS)

# Chạy kèm log ngắt/exception/CPU reset — dùng khi nghi ngờ triple fault hoặc
# ngắt phần cứng tới không đúng lúc. Dấu hiệu cần tìm trong log:
#   "Servicing hardware INT=0x08" -> IRQ0 đang bị hiểu nhầm thành Double Fault
#   "check_exception old: 0x8 new 0x..." -> đang trên đường tới triple fault
debug: $(IMG)
	$(QEMU) $(QEMU_FLAGS) -d int,cpu_reset -no-shutdown

# Chờ GDB kết nối ở cổng 1234 trước khi chạy lệnh đầu tiên:
#   gdb kernel/kernel.elf -ex "target remote :1234" -ex "hbreak kernel_main" -ex "continue"
gdb: $(IMG)
	$(QEMU) $(QEMU_FLAGS) -s -S

clean:
	$(MAKE) -C $(KERNEL_DIR) clean
	rm -f $(BOOT_BIN) $(IMG) kernel.padded.bin
