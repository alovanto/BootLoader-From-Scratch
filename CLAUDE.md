# my-os — Tài liệu kiến trúc dự án

> File này dành cho AI coding agent (Claude Code hoặc CLI tương tự) đọc để hiểu
> ngữ cảnh trước khi sửa/thêm code. Nếu dùng CLI khác Claude Code, có thể
> symlink hoặc copy file này thành tên tương ứng CLI đó yêu cầu.

## 1. Tổng quan dự án

Đây là một hệ điều hành tự viết từ đầu (từ bootloader tới kernel), mục tiêu
**kép**: (1) có một OS chạy được thật trên QEMU, (2) hiểu sâu cơ chế hoạt
động của OS lớn (Windows/Linux) thông qua việc tự tay implement phiên bản
đơn giản hoá của từng cơ chế đó.

**Nguyên tắc thiết kế xuyên suốt dự án** (agent PHẢI tuân theo khi đề xuất code mới):

- **Memory Manager đi trước mọi subsystem khác.** Process/Thread Manager,
  Driver, VFS đều cần cấp phát động (heap), heap cần Virtual Memory Manager,
  VMM cần Physical Frame Allocator. Không viết code cho tầng trên khi tầng
  dưới chưa xong — code sẽ không build/test được.
- **Có "lưới an toàn" trước khi viết logic phức tạp.** IDT + ISR tối thiểu
  phải tồn tại trước khi viết bất kỳ subsystem nào có khả năng gây page
  fault/GPF, nếu không debug sẽ chỉ thấy triple fault câm lặng.
- **Mỗi bước đều phải build và chạy được trên QEMU trước khi qua bước tiếp
  theo.** Không gộp nhiều thay đổi lớn cùng lúc mà chưa test.
- **Ưu tiên đúng và tường minh hơn tối ưu sớm.** Đây là dự án học tập; code
  rõ ràng, có comment giải thích "vì sao", quan trọng hơn code ngắn/nhanh.
- **C++ + Assembly, không Rust** (quyết định đã chốt, xem mục 9 để biết lý do).

## 2. Trạng thái hiện tại (cập nhật lần cuối: Physical Frame Allocator xong)

| Thành phần | Trạng thái | Ghi chú |
|---|---|---|
| Cấu trúc thư mục (`boot/`, `kernel/arch/x86_64/`, `kernel/mm/`...) | ✅ Xong | Đã tách khỏi layout phẳng ban đầu, khớp đúng mục 4 bên dưới; top-level `Makefile` đã hoàn thiện (assemble boot, gọi `make -C kernel`, ghép `os.img`, target `run`/`debug`) |
| Bootloader (`boot/boot.asm`) | ✅ Xong | Real → Protected → Long Mode, GDT, A20, page table 1GB identity-map |
| Nạp + bàn giao kernel | ✅ Xong | Bootloader tự load `kernel.bin` từ đĩa, copy lên 1MB, `jmp` vào `_start` |
| IDT tối thiểu + ISR | ✅ Xong | Bắt được Page Fault/GPF/Double Fault, in chẩn đoán thay vì triple fault |
| GDT riêng của kernel + TSS/IST | ✅ Xong | Double Fault chạy trên stack riêng (IST1, 8KB), an toàn dù stack chính hỏng |
| E820 memory map | ✅ Xong | `boot.asm` dò qua BIOS INT 15h/E820 ở Real Mode, ghi vào vật lý `0x20000`; `kernel/mm/e820.*` đọc lại + in ra VGA để verify. Bootloader vẫn identity-map cứng 1GB (chưa dùng map này để giới hạn phạm vi paging — đó là việc của VMM) |
| `.bss` được xoá chủ động | ✅ Xong | `kernel_entry.asm` zero `[__bss_start, __bss_end)` (symbol từ `linker.ld`) trước khi gọi `kernel_main` — bắt buộc từ khi PMM thêm bitmap 128KB vào `.bss`, không còn dựa vào zero-pad tình cờ của Makefile |
| Physical Frame Allocator | ✅ Xong | `kernel/mm/pmm.*` — bitmap 128KB quản lý 4GB, khoá đúng vùng 1MB thấp + vùng kernel thật; demo alloc/free trong `kernel_main.cpp` chứng minh frame free() được tái sử dụng đúng |
| PIC remap + PIT timer | ❌ Chưa làm | **Bẫy quan trọng khi cần tới**: PIC mặc định đè lên vector 8-15 (trùng CPU exception). Chỉ bắt buộc trước khi gọi `sti` (từ bước Process/Scheduler trở đi) |
| Virtual Memory Manager (thật) | ⚠️ Chỉ có bản thô | **Bước tiếp theo** — bootloader chỉ identity-map 1GB, PMM có thể đã cấp frame nằm ngoài vùng đó (xem mục 6) |
| Kernel Heap Allocator | ❌ Chưa làm | Cần VMM thật trước |
| Serial (COM1) debug output | ❌ Chưa làm | Đang dùng VGA text buffer thủ công, nên chuyển sang serial |
| Process/Thread Manager + Scheduler | ❌ Chưa làm | |
| Driver (keyboard, ATA disk) | ❌ Chưa làm | |
| VFS + Filesystem | ❌ Chưa làm | Đã bàn kiến trúc (page cache, inode/dentry) nhưng chưa code |
| Libc tối giản + Shell | ❌ Chưa làm | |

## 3. Ngăn xếp công nghệ

- **Ngôn ngữ:** C++17 (freestanding) cho logic kernel, x86-64 Assembly (NASM
  syntax) cho phần đụng trực tiếp CPU (bootloader, ISR entry stub, context switch).
- **Compiler:** `g++`/`clang++` với cờ freestanding bắt buộc:
  `-ffreestanding -fno-exceptions -fno-rtti -mno-red-zone -mcmodel=kernel
  -fno-pic -fno-pie`. Không dùng cross-compiler riêng (`x86_64-elf-gcc`) —
  dùng thẳng compiler host trên máy x86_64 Linux vì cùng ISA, chỉ cần đúng cờ
  freestanding là đủ, không cần build toolchain riêng.
- **Assembler:** NASM (`nasm -f bin` cho bootloader flat binary, `nasm -f elf64`
  cho object file của kernel).
- **Linker:** GNU `ld` với linker script tùy chỉnh (`kernel/linker.ld`).
- **Emulator:** QEMU (`qemu-system-x86_64`) — môi trường test chính, không
  cần phần cứng thật ở giai đoạn này.
- **Build system:** GNU Make (top-level Makefile gọi `make -C kernel`).

### Vì sao `-mno-red-zone` bắt buộc

System V AMD64 ABI cho phép hàm dùng 128 byte dưới RSP mà không cần trừ RSP
trước ("red zone"). Nhưng exception là bất đồng bộ — CPU có thể ngắt giữa
lúc một hàm đang dùng red zone và ghi đè lên đó khi push exception frame.
Tắt hẳn red zone cho **toàn bộ kernel** (không chỉ code ISR) để loại trừ lớp
bug này triệt để.

## 4. Cấu trúc thư mục

```
my-os/
├── boot/
│   └── boot.asm                     # Stage1+Stage2: BIOS → Real → Protected → Long Mode
├── kernel/
│   ├── Makefile
│   ├── linker.ld                    # Đặt kernel tại 0x100000 (1MB)
│   ├── kernel_entry.asm             # _start: dựng stack, gọi kernel_main
│   ├── kernel_main.cpp
│   ├── arch/x86_64/                 # Mọi thứ đụng trực tiếp CPU x86_64
│   │   ├── gdt.hpp / gdt.cpp        # GDT riêng của kernel (thay GDT tạm bootloader)
│   │   ├── gdt_flush.asm            # lgdt + reload CS bằng kỹ thuật far return
│   │   ├── tss.hpp / tss.cpp        # TSS + stack riêng (IST1) cho Double Fault
│   │   ├── idt.hpp / idt.cpp
│   │   ├── isr_stubs.asm
│   │   ├── interrupt_handlers.cpp
│   │   ├── io.hpp                   # [SẮP CÓ] inb/outb dùng chung
│   │   ├── pic.hpp / pic.cpp        # [SẮP CÓ]
│   │   └── pit.hpp / pit.cpp        # [SẮP CÓ]
│   ├── mm/                          # Memory Manager — không phụ thuộc CPU cụ thể
│   │   ├── e820.hpp / e820.cpp      # Đọc bản đồ RAM boot.asm dò được (xem mục 5.5)
│   │   ├── pmm.hpp / pmm.cpp        # Physical Frame Allocator, bitmap dựa trên E820 (mục 5.6)
│   │   ├── vmm.hpp / vmm.cpp        # [SẮP CÓ] Virtual Memory Manager thật
│   │   └── heap.hpp / heap.cpp      # [SẮP CÓ] operator new/delete
│   ├── drivers/
│   │   ├── serial.hpp / .cpp        # [SẮP CÓ]
│   │   ├── keyboard.hpp / .cpp      # [SẮP CÓ]
│   │   └── ata.hpp / .cpp           # [SẮP CÓ]
│   ├── proc/
│   │   ├── process.hpp / .cpp       # [SẮP CÓ] PCB/TCB
│   │   └── scheduler.hpp / .cpp     # [SẮP CÓ]
│   ├── fs/
│   │   ├── vfs.hpp / .cpp           # [SẮP CÓ]
│   │   └── fat32/                   # [SẮP CÓ]
│   └── include/kernel/              # Header dùng chung xuyên module
├── libc/                            # [SẮP CÓ] libc tối giản freestanding
├── userland/shell/                  # [SẮP CÓ] chương trình user-mode đầu tiên
├── docs/notes/                      # Ghi chú kiến trúc chi tiết từng cơ chế
├── Makefile                         # Top-level: build kernel, ghép os.img, run/debug
└── .gitignore
```

**Quy tắc phân chia `arch/x86_64/` vs phần còn lại:** bất kỳ file nào chứa
`asm volatile`, thao tác thanh ghi CPU (CR0-CR4, MSR), hoặc cấu trúc chỉ tồn
tại trên x86 (GDT, IDT, TSS) → thuộc `arch/x86_64/`. Logic thuật toán thuần
tuý (bitmap allocator, scheduler priority queue) → thuộc `mm/`, `proc/`... dù
project hiện không có kế hoạch port sang kiến trúc khác, ranh giới này giữ
code dễ đọc và dễ test độc lập.

## 5. Kiến trúc hệ thống chi tiết

### 5.1 Bootloader (`boot/boot.asm`)

Một file NASM flat-binary duy nhất, gồm 2 stage:

- **Stage 1** (512 byte đầu, MBR): BIOS load vào `0x7C00`. Đọc Stage2 +
  kernel.bin từ đĩa qua `INT 13h AH=42h` (LBA extended read, không phụ thuộc
  geometry đĩa).
- **Stage 2** (nằm ngay sau Stage1 trên đĩa, load tại `0x7E00`): còn ở
  Real Mode, thực hiện tuần tự:
  1. Đọc thêm `kernel.bin` vào vùng tạm `0x10000` (real mode không addressing
     thẳng qua 1MB được).
  2. Bật A20 gate (port `0x92`).
  3. `lgdt` nạp GDT flat model (base=0, limit=4GB).
  4. Set `CR0.PE=1`, far jump vào code segment 32-bit → Protected Mode.
  5. (32-bit) `rep movsd` copy kernel từ `0x10000` lên `0x100000` (giờ đã
     addressing thoải mái toàn bộ RAM nhờ segment flat).
  6. Dựng page table 4 cấp (PML4→PDPT→PD, dùng page 2MB) identity-map 1GB đầu.
  7. Bật PAE (`CR4`), bật `EFER.LME` (MSR `0xC0000080`), bật paging (`CR0.PG`),
     far jump vào code segment 64-bit → Long Mode.
  8. `jmp 0x100000` — bàn giao cho kernel, không quay lại.

**GDT layout** (`null=0x00, code32=0x08, data32=0x10, code64=0x18`) — con số
`0x18` là **ràng buộc ABI** với `kernel/arch/x86_64/idt.cpp`
(`KERNEL_CODE_SELECTOR`). Đổi thứ tự GDT entry phải sửa cả hai nơi.

### 5.2 Build pipeline & memory layout

```
boot.asm --nasm -f bin--> boot.bin  ─┐
                                       ├─ cat ──> os.img ──> QEMU boot tại 0x7C00
kernel/*.cpp,*.asm --g++/nasm/ld--> kernel.bin (đệm đủ KERNEL_SECTORS*512 byte) ─┘
```

- `linker.ld` đặt kernel bắt đầu tại `0x100000`, `ENTRY(_start)`.
- **Ràng buộc `KERNEL_SECTORS`:** hằng số này xuất hiện ở CẢ `boot/boot.asm`
  (tính LBA đọc đĩa + số byte copy) VÀ top-level `Makefile` (đệm kernel.bin
  cho đủ kích thước). Hiện = 128 (64KB). **Kernel vượt quá kích thước này
  phải tăng cả hai nơi cùng lúc**, nếu không bootloader copy thiếu mà không
  báo lỗi.
- `kernel_entry.o` phải là object file ĐẦU TIÊN trong danh sách link (`OBJS`
  trong `kernel/Makefile`) để `_start` nằm đúng byte đầu tiên của `.text`,
  khớp địa chỉ `0x100000`.

### 5.3 GDT riêng của kernel + TSS/IST (`kernel/gdt.*`, `kernel/tss.*`)

Từ bước này, kernel không còn dùng GDT tạm của bootloader nữa — `gdt_init()`
dựng GDT riêng (`null=0x00, code64=0x08, data64=0x10, TSS descriptor=0x18`,
TSS descriptor chiếm 16 byte = 2 slot) và nạp bằng `gdt_flush()` (assembly,
dùng kỹ thuật "far return" để reload CS vì CPU không cho `mov cs` trực tiếp).
`tss.cpp` dựng một `Tss` (104 byte, đúng chuẩn Intel SDM), dành 8KB làm stack
riêng cho `TSS.IST1`. `idt_init()` đăng ký `IST=1` CHỈ cho vector 8 (Double
Fault) — CPU sẽ ép chuyển sang stack sạch này bất kể RSP hiện tại ra sao,
loại bỏ nguy cơ triple fault khi double fault do stack chính đã hỏng.

**Thứ tự gọi bắt buộc trong `kernel_main()`:** `gdt_init()` → `idt_init()`.
Đảo ngược thứ tự sẽ khiến IDT tham chiếu tới GDT chưa active.

### 5.4 IDT / ISR / Exception handling (`kernel/idt.*`, `kernel/isr_stubs.asm`, `kernel/interrupt_handlers.cpp`)

- `idt.hpp`: định nghĩa `IdtEntry` (16-byte gate, layout khớp Intel/AMD
  manual), `Registers` (bản chụp thanh ghi khi exception xảy ra).
- **`Registers` KHÔNG có trường `rsp`/`ss`** — vì kernel hiện tại chạy 100%
  ring 0, CPU chỉ push `RIP, CS, RFLAGS` (+ error code nếu có) khi exception
  xảy ra, không push RSP/SS (chỉ push khi đổi mức đặc quyền). **Khi thêm
  user-mode (ring 3), PHẢI thêm lại 2 trường này** và sửa `isr_common` trong
  `isr_stubs.asm` tương ứng (đọc kỹ comment trong `idt.hpp` trước khi sửa).
- `isr_stubs.asm`: 32 stub cho vector 0-31, macro `ISR_ERR`/`ISR_NOERR` phân
  biệt vector nào CPU tự push error code thật (8, 10, 11, 12, 13, 14, 17) và
  vector nào cần push giả 0 để đồng nhất layout stack. Tất cả hội tụ vào
  `isr_common` — thứ tự push/pop 15 general-purpose register PHẢI khớp chính
  xác thứ tự field trong `struct Registers`.
- `interrupt_handlers.cpp`: `isr_handler()` — điểm xử lý C++ duy nhất, phân
  luồng theo `regs->vector`. Page Fault (14) đọc `CR2` lấy địa chỉ ảo gây
  lỗi, giải mã 3 bit thấp của error code (present/write/user). Hiện tại MỌI
  exception đều dừng máy có kiểm soát (`cli; hlt` vô hạn) — đây là hành vi
  TẠM THỜI của giai đoạn "lưới an toàn"; khi có Physical Frame Allocator,
  Page Fault hợp lệ (demand paging) phải sửa lỗi rồi `iretq` thay vì dừng.
- **IST = 1 CHỈ cho vector 8 (Double Fault)**, mọi vector khác vẫn IST=0
  (dùng chung stack hiện tại) — xem mục 5.3.

### 5.5 E820 memory map (`boot/boot.asm`, `kernel/mm/e820.*`)

- `detect_memory_e820` (trong Stage2 của `boot.asm`, vẫn ở Real Mode, TRƯỚC
  khi bật `CR0.PE`) gọi lặp `INT 15h, EAX=0xE820` — dịch vụ BIOS này CHỈ tồn
  tại ở Real Mode, đây là lý do bắt buộc dò trước khi chuyển Protected Mode,
  không thể hoãn lại làm ở kernel. Mỗi lần gọi BIOS trả về 1 entry 20-byte
  (`base:8, length:8, type:4`) và cập nhật `EBX` làm "continuation token";
  `EBX=0` sau lệnh gọi nghĩa là entry vừa nhận là entry cuối cùng.
- Kết quả ghi vào `E820_SEG:0000` = vật lý `0x20000` — chọn địa chỉ này vì
  nó nằm ngay sau vùng tạm nạp `kernel.bin` (`0x10000`-`0x20000`), không
  đụng Stage1/Stage2 (`0x7C00`-`0xBE00`) hay page table Long Mode
  (`0x1000`-`0x4000`). 4 byte đầu buffer là `entry_count` (word thấp), theo
  sau là mảng `E820Entry` 20-byte liên tiếp.
- `kernel/mm/e820.hpp` định nghĩa `E820_MAP_PHYS_ADDR = 0x20000` và
  `struct E820Entry` — layout PHẢI khớp chính xác những gì `boot.asm` ghi
  (xem ràng buộc ở mục 6). Vì địa chỉ này nằm trong 1GB đã identity-map,
  kernel đọc thẳng bằng con trỏ vật lý, không cần `map()` gì thêm.
- `e820_dump()` in bản đồ RAM ra VGA — được gọi từ `kernel_main.cpp` ngay
  sau khi IDT sẵn sàng, để verify bằng mắt trên QEMU. Đây cũng là lý do demo
  "cố tình gây Page Fault" trước đó bị bỏ: `isr_handler()` reset màn hình về
  dòng 0 mỗi khi có exception (xem `interrupt_handlers.cpp`), nên nếu vẫn
  giữ demo đó, nó sẽ xoá mất output E820 ngay khi chạy tới.
- Type trong mỗi entry theo đúng chuẩn BIOS/ACPI (`1=Usable, 2=Reserved,
  3=ACPI Reclaimable, 4=ACPI NVS, 5=Bad`) — Physical Frame Allocator (mục
  5.6) CHỈ được cấp phát frame từ vùng `Usable`.

### 5.6 Physical Frame Allocator (`kernel/mm/pmm.hpp`/`pmm.cpp`)

Bitmap tĩnh (128KB, nằm trong `.bss`) quản lý tối đa `MAX_SUPPORTED_RAM`
(4GB) — mỗi bit ứng với một frame 4KB. `pmm_init()` dựng bitmap theo đúng
4 bước, thứ tự bắt buộc (bước sau đè lên bước trước):
1. Mặc định MỌI frame trong phạm vi hỗ trợ là "đã dùng" — an toàn theo
   thiết kế, không tin tưởng bất kỳ vùng nào cho tới khi được xác nhận.
2. Mở khoá (đánh dấu trống) các frame nằm trong vùng `Usable` theo E820.
3. Khoá LẠI toàn bộ 1MB thấp bất kể E820 nói gì — đây là nơi chứa BIOS data
   area, tàn dư Stage1/Stage2, và chính bảng E820 đang được đọc ở `0x20000`.
4. Khoá vùng kernel thật `[0x100000, __kernel_end)` — tự động bao gồm cả
   chính bitmap vì bitmap nằm trong `.bss` của kernel.

`pmm_alloc_frame()`/`pmm_free_frame()` dùng con trỏ `next_free_hint` để
tránh quét lại từ đầu bitmap mỗi lần cấp phát; `free_frame()` bảo vệ
double-free bằng cách kiểm tra frame đã ở trạng thái free chưa trước khi
đếm lại.

**Vì sao bước này buộc phải sửa `kernel_entry.asm`/`linker.ld` trước:**
bitmap 128KB nằm trong `.bss`, và kernel trước đó chưa hề chủ động zero
`.bss` — nó chạy đúng chỉ nhờ `.bss` cũ đủ nhỏ để nằm gọn trong vùng
zero-pad tình cờ của top-level Makefile khi ghép `os.img`. Thêm bitmap lớn
phá vỡ giả định ngầm đó (bitmap sẽ chứa rác thay vì toàn 0), nên
`kernel_entry.asm` giờ zero `[__bss_start, __bss_end)` một cách tường minh
(2 symbol `linker.ld` export), không phụ thuộc kích thước file trên đĩa nữa.

**Giới hạn đã biết (xem mục 6):** `boot.asm` chỉ identity-map 1GB đầu, nên
PMM có thể cấp một frame nằm ngoài vùng đó nếu RAM máy đủ lớn — truy cập
frame đó sẽ Page Fault cho tới khi VMM (bước tiếp theo) tự map on-demand.

## 6. Ràng buộc/hợp đồng quan trọng (đọc trước khi sửa code liên quan)

| Ràng buộc | Nằm ở đâu | Hậu quả nếu phá vỡ mà không đồng bộ |
|---|---|---|
| GDT code64 selector = `0x08` | `kernel/gdt.hpp` (`KERNEL_CODE_SELECTOR`) ↔ `kernel/idt.cpp` | IDT trỏ sai selector → GPF ngay exception đầu tiên. **Lưu ý:** GDT của `boot.asm` (code64=`0x18`) chỉ dùng trong lúc bootloader còn chạy — kernel có GDT riêng, độc lập, load lại từ đầu qua `gdt_init()` |
| `gdt_init()` phải chạy TRƯỚC `idt_init()` | `kernel_main.cpp` | IDT gate tham chiếu GDT chưa active → hành vi không xác định khi exception xảy ra |
| `KERNEL_SECTORS = 128` | `boot/boot.asm` ↔ top-level `Makefile` | Bootloader copy thiếu kernel, chạy sai không rõ lý do |
| Kernel base = `0x100000` | `kernel/linker.ld` (`. = 0x100000`) ↔ `boot/boot.asm` (`KERNEL_PHYS_ADDR`) | `jmp` vào sai địa chỉ, thường dẫn tới nhảy vào vùng nhớ rác |
| `Registers` không có rsp/ss | `idt.hpp` ↔ `isr_stubs.asm` (`isr_common`) ↔ giả định "ring0-only" | Đọc lệch dữ liệu thanh ghi khi có exception, sai âm thầm không crash rõ ràng |
| `kernel_entry.o` phải link đầu tiên | `kernel/Makefile` (`OBJS`) | `_start` không nằm ở byte 0 của kernel.bin → bootloader nhảy vào giữa hàm khác |
| `E820_MAP_PHYS_ADDR = 0x20000` | `boot/boot.asm` (`E820_SEG:E820_OFF`) ↔ `kernel/mm/e820.hpp` | Kernel đọc nhầm vùng nhớ khác thành bản đồ RAM → PFA cấp phát rác |
| Layout `E820Entry` = 20 byte (`base:8, length:8, type:4`) | `boot/boot.asm` (`detect_memory_e820`) ↔ `kernel/mm/e820.hpp` (`struct E820Entry`) | Đọc lệch field, `base`/`length` sai giá trị mà không crash rõ ràng |
| `__bss_start`/`__bss_end`/`__kernel_end` | `kernel/linker.ld` ↔ `kernel_entry.asm` (xoá `.bss`) ↔ `kernel/mm/pmm.cpp` (khoá vùng kernel) | Thiếu 1 trong 3 chỗ dùng sai symbol → `.bss` không được zero đúng (bitmap PMM chứa rác lúc khởi động), hoặc PMM tự cấp đè lên chính kernel đang chạy |
| `MAX_SUPPORTED_RAM = 4GB` trong PMM | `kernel/mm/pmm.hpp` | RAM thật > 4GB bị bỏ qua an toàn (không cấp phát được phần vượt), không gây lỗi nhưng lãng phí RAM |
| Identity-map bootloader chỉ phủ 1GB | `boot/boot.asm` (`setup_page_tables`, PD 512 entry × 2MB) ↔ giả định của PMM/VMM | PMM có thể trả frame nằm ngoài 1GB → truy cập gây Page Fault cho tới khi VMM tự map on-demand |

## 7. Nợ kỹ thuật đã biết (technical debt)

1. **PIC chưa remap — BẪY khi bật hardware interrupt.** PIC 8259 mặc định
   ánh xạ IRQ0-7 vào vector 8-15, TRÙNG với CPU exception vector (vector 8 =
   Double Fault!). Bắt buộc remap PIC (ICW1-ICW4) sang vector 32-47 TRƯỚC KHI
   `sti`, nếu không timer tick sẽ trông giống double fault giả. (Chưa cần
   ngay — chỉ bắt buộc trước khi có subsystem nào gọi `sti`, tức Process/
   Scheduler; PFA/VMM/Heap không đụng tới ngắt phần cứng.)
2. **Chưa có serial (COM1) debug output.** Đang in chẩn đoán qua VGA text
   buffer thủ công (80x25, không copy-paste được). Nên thêm driver serial
   sớm để log qua `qemu -serial stdio`.
3. **Chưa có `kprintf`/`panic()` dùng chung.** Mỗi chỗ báo lỗi tự viết lại
   logic in hex/text (`interrupt_handlers.cpp`, `mm/e820.cpp` đều tự có
   `to_hex()`/`vga_print` riêng) — nên gom thành một hàm chung trước khi viết
   thêm nhiều subsystem báo lỗi khác nhau.

## 8. Lộ trình phát triển tiếp theo (theo đúng thứ tự phụ thuộc)

```
Virtual Memory Manager thật (mm/vmm) — map()/unmap() linh hoạt, thay identity-map cứng
   ↓ (LƯU Ý: boot.asm chỉ identity-map 1GB — VMM phải tự map on-demand
   ↓  bất kỳ frame nào PMM cấp nằm ngoài vùng đó, xem mục 6)
Kernel Heap Allocator (mm/heap) — mở khoá operator new/delete cho MỌI subsystem sau
   ↓
PIC remap + PIT timer (arch/x86_64) — hoãn tới đây, ngay trước khi cần cho Scheduler
   ↓
Process/Thread Manager + Scheduler (proc/) — cần heap để cấp PCB/TCB
   ↓
Driver: keyboard, ATA disk (drivers/) — cần heap cho buffer
   ↓
VFS + Filesystem FAT32 (fs/) — cần driver disk + heap cho inode/dentry
   ↓
Libc tối giản + Shell (libc/, userland/shell/) — bước đầu tiên có user-mode (ring 3)
```

**Điểm ngoặt kiến trúc quan trọng nhất trong lộ trình:** khi tới bước
Libc/Shell, đây là lần đầu tiên hệ thống chạy code ở **ring 3**. Tại thời
điểm đó, PHẢI quay lại sửa: (a) `Registers` struct thêm `rsp`/`ss`, (b)
`isr_common` xử lý đúng 5 trường CPU push thay vì 3, (c) TSS.RSP0 phải trỏ
tới kernel stack hợp lệ để CPU tự nạp khi có syscall/interrupt từ ring 3.
Đừng ngạc nhiên nếu agent tương lai cần sửa lại các file trong mục 6 — đây
là thay đổi kiến trúc đã biết trước, không phải bug.

## 9. Quyết định kiến trúc đã chốt (không cần tranh luận lại trừ khi có lý do mới)

- **C++ + Assembly, không Rust.** Lý do: khớp trực tiếp với kiến thức Windows
  kernel (WDM/KMDF) người phát triển đang học song song từ 2 tài liệu tham
  khảo Windows kernel internals; Rust được cân nhắc lại sau khi có kernel v1
  chạy ổn để so sánh trải nghiệm, không phải ngay từ đầu.
- **QEMU làm môi trường test chính**, không dùng phần cứng thật ở giai đoạn
  này. Bochs dùng đối chiếu khi nghi ngờ UB mà QEMU không phát hiện.
- **Không dùng cross-compiler riêng** (`x86_64-elf-gcc`) — dùng thẳng
  `g++`/`clang++` trên host x86_64 Linux với cờ freestanding, vì cùng ISA.
- **Không dùng GRUB/Limine** — tự viết bootloader từ đầu, vì mục tiêu chính
  là học cơ chế, không phải tốc độ ra sản phẩm.

## 10. Cách build & test

```bash
# Build toàn bộ (tự gọi make -C kernel, ghép os.img)
make

# Chạy trên QEMU
make run

# Chạy kèm log exception/CPU reset — dùng khi nghi ngờ triple fault
make debug
# tương đương: qemu-system-x86_64 -drive format=raw,file=os.img \
#              -d int,cpu_reset -no-reboot -no-shutdown

# Debug từng bước bằng GDB (thêm vào lệnh QEMU khi cần)
qemu-system-x86_64 -drive format=raw,file=os.img -s -S
# rồi ở terminal khác: gdb -ex "target remote :1234"
```

**Trước khi coi một thay đổi là "xong":** phải thấy được kết quả tương ứng
trên màn hình QEMU (ví dụ dòng chữ debug, hoặc chẩn đoán exception), không
chỉ dựa vào "build không lỗi". Build thành công không đảm bảo logic đúng ở
tầng CPU thấp — rất nhiều lỗi (sai selector, sai thứ tự CR0/CR4/EFER, quên
far jump) chỉ lộ ra khi chạy thật.

## 11. Bảng thuật ngữ nhanh

| Thuật ngữ | Định nghĩa ngắn gọn |
|---|---|
| GDT | Bảng mô tả segment CPU dùng ở Protected/Long Mode |
| IDT | Bảng 256 entry, mỗi entry trỏ tới ISR xử lý 1 vector interrupt/exception |
| ISR | Đoạn code chạy khi interrupt/exception xảy ra, lưu/khôi phục thanh ghi, gọi handler C++ |
| GPF (#13) | Vi phạm bảo vệ không liên quan trang (sai selector, lệnh đặc quyền ở ring sai) |
| Page Fault (#14) | MMU gặp page chưa map/không đủ quyền; thường hợp lệ (demand paging, COW) |
| Double Fault (#8) | Fault xảy ra ngay trong lúc xử lý fault khác; không sửa được → Triple Fault (CPU tự reset) |
| TSS | Cấu trúc CPU dùng để giữ stack pointer dự phòng (RSP0-2, IST1-7), khai báo qua GDT |
| IST | Cơ chế ép CPU chuyển sang stack cố định khi 1 exception cụ thể xảy ra, bất kể RSP hiện tại |
| PIC/APIC | Bộ điều khiển ngắt phần cứng, ánh xạ IRQ thiết bị vào vector CPU |
| TLB | Cache nhỏ trong CPU giữ bản dịch địa chỉ ảo→vật lý gần nhất |
| PFA (Physical Frame Allocator) | Theo dõi frame vật lý 4KB nào đang trống/đã cấp |
| VMM (Virtual Memory Manager) | Quản lý ánh xạ địa chỉ ảo→vật lý, page table, address space riêng từng process |
| VFS | Lớp trừu tượng giữa syscall file và filesystem driver cụ thể (FAT32, ext4...) |
| Page cache | Cache nội dung file theo page, khoá tra cứu = (file/inode, offset căn theo page) |

## 12. Nguyên tắc khi agent làm việc tiếp trên project này

1. Đọc mục 6 (ràng buộc) trước khi sửa bất kỳ file nào trong `boot/`,
   `kernel/arch/x86_64/`, hoặc `kernel/linker.ld` — các con số ở đó liên kết
   chặt với nhau, sửa một chỗ mà quên chỗ còn lại sẽ gây lỗi khó debug.
2. Theo đúng thứ tự phụ thuộc ở mục 8 — không viết Process Manager trước khi
   có Kernel Heap Allocator, không viết VMM thật trước khi có Physical Frame
   Allocator.
3. Mọi subsystem mới liên quan CPU trực tiếp (thanh ghi, port I/O, MSR) đặt
   trong `kernel/arch/x86_64/`; logic thuật toán thuần tuý đặt trong thư mục
   tương ứng (`mm/`, `proc/`, `fs/`).
4. Sau mỗi thay đổi, cập nhật bảng trạng thái ở mục 2 và cập nhật
   `docs/notes/` nếu thay đổi liên quan tới cơ chế quan trọng.
5. Không tự ý đổi sang Rust hoặc thêm cross-compiler riêng — đây là quyết
   định đã chốt ở mục 9, chỉ đổi khi người dùng yêu cầu rõ ràng.
6. Comment giải thích "vì sao" bằng tiếng Việt, theo đúng văn phong đã dùng
   trong toàn bộ codebase hiện có — giữ nhất quán.
