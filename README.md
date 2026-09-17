# BootLoader-From-Scratch
Hệ điều hành x86_64 tự viết từ đầu (bootloader → kernel), bằng C++17
freestanding + NASM assembly, chạy trên QEMU.

- **Kiến trúc, quy ước, trạng thái hiện tại, lộ trình:** xem [`CLAUDE.md`](CLAUDE.md)
  — đọc file này trước khi sửa bất kỳ thứ gì.
- **Ghi chú kiến trúc chi tiết từng cơ chế:** [`docs/notes/`](docs/notes/).

## Build & chạy

Cần `g++`, `nasm`, `ld`, `objcopy`, `qemu-system-x86_64` trên máy Linux
x86_64 (hoặc WSL) — xem lý do không cần cross-compiler riêng ở mục 3,
CLAUDE.md.

```bash
make        # build boot/boot.bin + kernel/kernel.bin, ghép thành os.img
make run    # chạy os.img trên QEMU
make debug  # chạy kèm log exception/CPU reset (dùng khi nghi triple fault)
make clean
```

## Cấu trúc thư mục

```
boot/               bootloader (real → protected → long mode)
kernel/
  arch/x86_64/      GDT, TSS, IDT, ISR — mọi thứ đụng trực tiếp CPU
  mm/               physical/virtual memory manager, heap [SẮP CÓ]
  drivers/          serial, keyboard, ATA [SẮP CÓ]
  proc/             process/thread manager, scheduler [SẮP CÓ]
  fs/               VFS + filesystem [SẮP CÓ]
  include/kernel/   header dùng chung xuyên module [SẮP CÓ]
libc/               libc tối giản freestanding [SẮP CÓ]
userland/shell/     chương trình user-mode đầu tiên [SẮP CÓ]
docs/notes/         ghi chú kiến trúc chi tiết
```

Chi tiết đầy đủ từng thư mục: xem mục 4, CLAUDE.md.
