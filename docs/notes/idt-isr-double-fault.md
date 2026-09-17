# IDT tối thiểu + ISR cho Page Fault / GPF / Double Fault

## Build

Cần `g++`, `nasm`, `ld`, `objcopy` (đã có trong hầu hết distro Linux qua `build-essential` + `nasm`).

```bash
make
```

Sinh ra `kernel.bin` — một flat binary bắt đầu bằng `_start` (trong `kernel_entry.asm`), đặt tại địa chỉ 0x100000 theo `linker.ld`.

Đã test-compile 2 file C++ (`idt.cpp`, `interrupt_handlers.cpp`) và validate logic của `isr_stubs.asm` (thứ tự push/pop, ABI gọi hàm, `iretq`) ngay trong lúc viết — cú pháp NASM thật thì cần máy có `nasm` để assemble, môi trường tạo file này không có sẵn.

## Vì sao có `-mno-red-zone`

System V AMD64 ABI cho phép một hàm dùng 128 byte ngay dưới RSP (gọi là "red zone") mà không cần trừ RSP trước — an toàn với lời gọi hàm bình thường vì CPU không tự ý đụng vào đó. Nhưng **exception là bất đồng bộ**: CPU có thể ngắt bất kỳ lúc nào, kể cả giữa lúc một hàm đang dùng red zone, rồi push dữ liệu (RIP, CS, RFLAGS...) đè thẳng lên đúng vùng đó trước khi `isr_common` kịp làm gì. Kết quả: dữ liệu hàm đang dùng bị ghi đè âm thầm — một bug cực khó tìm vì chỉ xảy ra khi exception "trúng thời điểm xấu". Tắt hẳn `-mno-red-zone` cho toàn bộ kernel là cách chuẩn để loại trừ lớp bug này ngay từ đầu.

## Đã nối vào `boot.asm`

`boot.asm` (ở thư mục cha) giờ đã: (1) Stage2 đọc thêm `kernel.bin` từ đĩa vào vùng tạm 0x10000, (2) ngay khi vào 32-bit Protected Mode, `rep movsd` copy nó lên đúng 0x100000 (khớp `linker.ld`), (3) sau khi vào Long Mode, `jmp 0x100000` bàn giao quyền điều khiển cho `_start` trong `kernel_entry.asm`. Build toàn bộ bằng `make` ở thư mục cha (`os-bootloader/`), không phải build riêng trong `kernel/` nữa — Makefile gốc tự gọi `make -C kernel` trước khi ghép ảnh đĩa.

**Ràng buộc cần nhớ:** `KERNEL_SECTORS` phải giống nhau ở cả `boot.asm` và Makefile gốc (hiện = 128, tức 64KB). Nếu kernel phình to hơn 64KB, phải tăng cả hai nơi cùng lúc, nếu không bootloader sẽ copy thiếu và kernel chạy sai mà không báo lỗi rõ ràng.

## Double Fault (vector 8) — ĐÃ vá bằng TSS/IST

`gdt.cpp` + `tss.cpp` dựng GDT riêng của kernel (thay GDT tạm của bootloader),
có một TSS descriptor. `tss.cpp` dành riêng 8KB làm stack cho Double Fault
(`TSS.IST1`), và `idt.cpp` đăng ký `IST=1` cho đúng vector 8. Khi Double
Fault xảy ra — kể cả do stack chính đã hỏng — CPU ép chuyển sang stack sạch
này trước khi gọi handler, loại bỏ nguy cơ triple fault dây chuyền.

**Lưu ý selector đã đổi:** từ bước này, `KERNEL_CODE_SELECTOR` chuyển từ
`0x18` (GDT tạm của bootloader) sang `0x08` (GDT riêng của kernel, định
nghĩa trong `gdt.hpp`). `kernel_main()` phải gọi `gdt_init()` TRƯỚC
`idt_init()` — thứ tự này bắt buộc, không được đảo.
