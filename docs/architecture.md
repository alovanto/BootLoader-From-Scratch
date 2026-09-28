# my-os — Kiến trúc tổng thể & lộ trình

> **Tài liệu này là gì.** Bản thiết kế kiến trúc đầy đủ cho my-os, từ bootloader
> tới shell ở ring 3: các lớp, cách chúng gọi nhau, các "hợp đồng" phải chốt
> trước khi code, phác thảo code chi tiết cho từng thành phần, và lộ trình
> M0→M9 có tiêu chí kiểm chứng trên QEMU.
>
> **Quan hệ với `CLAUDE.md`.** `CLAUDE.md` = trạng thái hiện tại + quy tắc làm
> việc. File này = kiến trúc đích + lý do. Khi hai file mâu thuẫn, xem mục 9
> (danh sách chỗ `CLAUDE.md` cần sửa).
>
> **Trạng thái kiểm chứng của code trong tài liệu.** Mọi đoạn code ở đây là
> *phác thảo thiết kế* — **chưa được compile/assemble/chạy** (máy soạn tài liệu
> không có `nasm`/`g++`/`qemu`). Logic, offset, số MSR, layout struct đã được
> đối chiếu với Intel SDM / OSDev, nhưng phải qua đúng quy trình "build + thấy
> kết quả trên QEMU" (CLAUDE.md mục 10) trước khi coi là đúng.
>
> **Cách đọc cho người đang nghiên cứu kiến trúc:** đọc mục 2 (bức tranh toàn
> cảnh) → mục 3 (các luồng tương tác — phần quan trọng nhất để hiểu các thành
> phần nói chuyện với nhau thế nào) → mục 4 (hợp đồng) → chỉ đọc mục 5 (chi
> tiết) cho thành phần đang làm.

---

## Mục lục

1. [Kết quả phân tích hiện trạng](#1-kết-quả-phân-tích-hiện-trạng)
2. [Bức tranh toàn cảnh](#2-bức-tranh-toàn-cảnh)
3. [Các luồng tương tác xuyên thành phần](#3-các-luồng-tương-tác-xuyên-thành-phần)
4. [Hợp đồng nền tảng — chốt trước khi code](#4-hợp-đồng-nền-tảng--chốt-trước-khi-code)
5. [Thiết kế chi tiết từng tầng](#5-thiết-kế-chi-tiết-từng-tầng)
6. [Build, test, debug](#6-build-test-debug)
7. [Lộ trình M0 → M9](#7-lộ-trình-m0--m9)
8. [Bảng ràng buộc mới](#8-bảng-ràng-buộc-mới)
9. [Những chỗ CLAUDE.md cần sửa](#9-những-chỗ-claudemd-cần-sửa)
10. [Tài liệu tham khảo](#10-tài-liệu-tham-khảo)

---

## 1. Kết quả phân tích hiện trạng

Phân tích dựa trên đọc toàn bộ code tại commit `0641ade` (≈1.600 dòng:
`boot/boot.asm`, `kernel/**`, 2 Makefile) và đối chiếu với bản review ngoài.

### 1.1 Lỗi phát hiện thêm (bản review không nêu)

#### P0 — Ngắt đang BẬT suốt từ Stage1 tới hết kernel

**Bằng chứng:**
- `boot/boot.asm:53` — Stage1 gọi `sti` (cần cho BIOS `INT 13h`).
- Stage2 (`boot.asm:109-150`) gọi tiếp `INT 13h`, `INT 15h` rồi `lgdt` + `mov cr0`
  — **không có `cli`** ở đâu cả.
- `protected_mode_entry`, `long_mode_entry`, `kernel/kernel_entry.asm:22-40`
  — không có `cli` (chỉ có sau khi `kernel_main` return, không bao giờ xảy ra).
- `kernel/kernel_main.cpp:89-91` — vòng `hlt` cuối cùng chạy với IF=1.

**Hậu quả dự đoán (phân tích tĩnh):**
1. *Cửa sổ từ `mov cr0` tới `lidt` trong kernel:* IDTR vẫn trỏ IVT real mode
   (base 0, limit 0x3FF). Một tick PIT rơi vào đây → CPU đọc "gate" rác →
   #GP → #DF → **triple fault ngẫu nhiên** (xác suất thấp vì cửa sổ ngắn so với
   chu kỳ 55ms, nên rất khó tái hiện — kiểu lỗi tệ nhất).
2. *Sau `idt_init()`:* PIC chưa remap nên IRQ0 (timer BIOS, ~18.2Hz) = vector 8
   = stub `ISR_ERR 8`. `hlt` cuối `kernel_main` gần như chắc chắn bị đánh thức
   trong ≤55ms → màn hình "**Double Fault**" giả.
3. IRQ không push error code nhưng stub vector 8 tưởng là có → `Registers` bị
   lệch 1 ô. Dấu hiệu nhận biết trên màn hình: **`Error code` trông như địa
   chỉ kernel `0x00000000001xxxxx`, còn `RIP` = `0x0000000000000008`** (thực
   ra là CS). Bấm một phím → IRQ1 = vector 9 → "Coprocessor Overrun".

**Cách xác nhận:** `make debug` → log `-d int` sẽ có dòng
`Servicing hardware INT=0x08`.

**Sửa (M0, mục 7):** `cli` ngay trước `lgdt` trong Stage2; `cli` + `cld` đầu
`_start`; remap + mask toàn bộ PIC trước khi bao giờ `sti` lại.

#### P1 — Các lỗi nhỏ hơn

| # | Vị trí | Vấn đề | Hậu quả |
|---|---|---|---|
| a | `isr_stubs.asm:53` | Vector 21 (#CP) được khai `ISR_NOERR`, nhưng Intel SDM bảng 6-1: #CP **có** error code | Lệch frame nếu CET bật (hiếm trên QEMU) |
| b | `Makefile:37` | `truncate -s 65536` **cắt cụt** kernel.bin nếu kernel > 64KB, không báo lỗi | Kernel bị cắt đuôi âm thầm — đúng loại lỗi "chạy sai không rõ lý do" |
| c | `boot.asm:30` | `KERNEL_SECTORS=128` đọc 1 lần; đặc tả EDD giới hạn 127 block/lần gọi | Chạy được trên SeaBIOS/QEMU, hỏng trên BIOS thật khác |
| d | `kernel_entry.asm:33-37`, `isr_stubs.asm:88-91` | Không `cld` trước `rep stosb` / trước khi gọi C++ | SysV ABI yêu cầu DF=0 khi vào hàm; hiện chưa ai set DF nên chưa lộ |
| e | `linker.ld` | Không khai báo `.init_array`, `.got`… → orphan section do `ld` tự xếp | Constructor global không chạy; section có thể rơi ra ngoài vùng bootloader copy |
| f | `isr_stubs.asm:111`, `idt.hpp:26,32` | Comment "CPU chỉ pop RIP, CS, RFLAGS" sai ở Long Mode (xem 1.2) | Dẫn tới kế hoạch sai ở CLAUDE.md mục 8(b) |
| g | `boot.asm:251` | Comment "bit L trong access byte" — L nằm ở byte flags (nibble cao byte thứ 7) | Chỉ sai comment |

### 1.2 Đối chiếu từng điểm của bản review

| Điểm | Kết luận | Ghi chú kiểm chứng |
|---|---|---|
| 1.1 Long Mode luôn push SS:RSP | ✅ **Đúng** | Intel SDM Vol.3 §6.14.2: ở 64-bit mode CPU push SS:RSP vô điều kiện và căn RSP về bội 16 trước khi push. Code hiện chưa crash vì `iretq` tự pop đủ 5 giá trị; struct chỉ "không nhìn thấy" 2 ô cuối. Kiểm tra căn stack: CPU 40/48 byte + stub 16/8 byte + 15 GPR = 176 byte = 11×16 → RSP%16==0 trước `call isr_handler` ✓ |
| 1.2 64KB đè bảng E820 | ✅ **Đúng, và cụ thể hơn** | Kernel tạm `0x10000`–`0x1FFFF`, E820 tại `0x20000`. Stage2 nạp kernel **trước** rồi mới dò E820 → tăng lên 129 sector thì **E820 ghi đè đuôi kernel**. Thêm lỗi (b), (c) ở trên |
| 1.3 `-mcmodel=kernel` ở 0x100000 "chạy nhờ may mắn" | ⚠️ **Hiệu chỉnh** | Không phải may mắn: model kernel sinh relocation `R_X86_64_32S`, mọi địa chỉ < 2GB đều hợp lệ; nếu sai `ld` sẽ báo "relocation truncated". **Nhưng kết luận kiến trúc vẫn đúng:** phải lên higher-half trước khi có user mode |
| 1.4 Map on-demand frame ngoài 1GB là sai hướng | ✅ **Đúng** | Thêm một chi tiết: khi dựng direct map, các page table cho vùng >1GB phải lấy từ frame **<1GB** (đã map sẵn) — cần API `pmm_alloc_frame_below()` (mục 5.5) |
| 1.5 Cờ compiler chưa đủ | ✅ Đúng về hướng, ⚠️ 3 hiệu chỉnh | (1) `-fno-use-cxa-atexit` khiến g++ đăng ký destructor qua `atexit()` → lại phải cung cấp `atexit`; đơn giản hơn là giữ mặc định và stub `__cxa_atexit` + `__dso_handle`. (2) `kernel/Makefile:47-51` **đã giữ** `kernel.elf`. (3) Hiện build ở `-O0` (không có cờ `-O`) nên SSE hầu như chưa được sinh ra — lỗi sẽ lộ ngay khi bật `-O2` |
| 1.6 IDT 32 vector, spurious IRQ trước `sti` | ⚠️ **Hiệu chỉnh** | Spurious IRQ 7/15 vẫn là ngắt che được (maskable) → IF=0 thì không tới. **Nhưng kết luận "đừng hoãn PIC" còn đúng hơn review nghĩ**, vì IF thực tế đang = 1 (P0) |
| 1.6 IST riêng cho NMI/#MC | ✅ Đúng | Bắt buộc từ lúc có `SYSCALL` (có đoạn ring 0 chạy trên stack user) |
| 1.7 GDT layout cho SYSRET | ✅ Đúng | SYSRET: `CS = STAR[63:48]+16`, `SS = STAR[63:48]+8` → user data phải đứng ngay trước user code64 |
| 1.8 Thiếu đồng bộ hoá, kernel thread, ring 3 quá muộn | ✅ Đúng | Được đưa vào lộ trình mục 7 |
| Phần 2–3 (NT layering, BootInfo, HHDM, IRQL, M0–M8) | ✅ Chấp nhận làm khung | Mục 4–7 cụ thể hoá thêm: disk manifest, kernel header, bất biến IRQL cho context switch, exception table cho `copy_from_user`, luồng IRP |

### 1.3 Những gì đang tốt — giữ nguyên

- **Tư duy "lưới an toàn trước"** (IDT + IST cho #DF trước PMM) — đúng.
- **PMM "mặc định khoá hết, chỉ mở vùng Usable"** (`pmm.cpp:73-127`) — an toàn
  theo thiết kế; chỉ cần mở rộng API, không viết lại.
- **Zero `.bss` tường minh** bằng symbol linker — đúng.
- **`gdt_flush` bằng far return, `movzx rsi, si`** — cẩn thận đúng chỗ.
- **Comment giải thích "vì sao"** — tiếp tục giữ văn phong này.
- **Các quyết định đã chốt ở CLAUDE.md mục 9** (C++/asm, QEMU, tự viết
  bootloader, không cross-compiler) — kiến trúc dưới đây không đổi quyết định
  nào trong số đó.

---

## 2. Bức tranh toàn cảnh

### 2.1 Sơ đồ phân lớp

```
 ring 3 ┌──────────────────────────────────────────────────────────────────┐
        │  userland/shell, init, programs      libc (crt0, syscall stubs) │
        └───────────────────────────────┬──────────────────────────────────┘
                                        │ SYSCALL (rax=số hiệu, rdi..r9=tham số)
 ═══════════════════════════════════════╪══════════════════════════════ ranh giới ring
 ring 0                                 ▼
 ┌─────────────────────────── Dịch vụ hệ thống (syscall handlers) ──────────────────────┐
 │  sys_read/write/open (io,fs)   sys_spawn/exit/wait (ps)   sys_mmap (mm)   sys_sleep (ke) │
 └───────┬────────────────────────────┬──────────────────────┬─────────────────────────┘
         │                            │                      │
 ┌───────▼────────┐  ┌────────────────▼──────┐  ┌────────────▼──────────┐
 │ fs/  VFS       │  │ ps/  Process, Thread, │  │ ob/ Object header,    │
 │  tarfs, FAT32  │  │   ELF loader          │  │  refcount, HandleTable│
 │  block cache   │  └───────┬───────────────┘  └───────────────────────┘
 └───────┬────────┘          │
 ┌───────▼────────────────┐  │     ┌─────────────────────────────────────────────┐
 │ io/  DriverObject,     │  │     │ mm/  PMM (frame) → VMM (page table, HHDM)   │
 │  DeviceObject, IRP     │  │     │      → AddressSpace/VMA → heap (kmalloc)    │
 └───────┬────────────────┘  │     │      → page fault handler, copy_from_user   │
 ┌───────▼────────────────┐  │     └─────────────────────────────────────────────┘
 │ drivers/ serial, kbd,  │  │     ┌─────────────────────────────────────────────┐
 │  tty, ata, vga console │  └────►│ ke/  IRQL, spinlock, DPC, timer, scheduler, │
 └───────┬────────────────┘        │      wait queue, event, mutex               │
         │                         └──────────────────┬──────────────────────────┘
 ┌───────▼────────────────────────────────────────────▼──────────────────────────┐
 │ arch/x86_64 (≈ HAL): GDT/TSS/IDT, isr stubs, interrupt dispatch, PIC, PIT,   │
 │  per-CPU (GS), context switch, syscall entry, CR/MSR/port I/O, user copy asm  │
 └───────────────────────────────────────────────────────────────────────────────┘
 ┌───────────────────────────────────────────────────────────────────────────────┐
 │ lib/: kprintf, panic/KASSERT, string (asm), intrusive list, C++ runtime, ktest │  ← mọi tầng dùng
 └───────────────────────────────────────────────────────────────────────────────┘
        ▲ BootInfo (con trỏ trong RDI)
 ┌──────┴────────────────────────────────────────────────────────────────────────┐
 │ boot/: Stage1 (MBR) → Stage2 (E820, nạp kernel+initrd, page table, Long Mode) │
 └───────────────────────────────────────────────────────────────────────────────┘
```

### 2.2 Quy tắc phụ thuộc (bắt buộc)

1. **Chỉ gọi xuống, không gọi lên.** Tầng dưới muốn "báo" tầng trên thì dùng
   callback đã đăng ký (vd `irq_register`) hoặc cơ chế trì hoãn (DPC, event).
2. **`lib/` không phụ thuộc gì** ngoài `arch/` tối thiểu (port I/O cho serial).
3. **`mm/` không phụ thuộc `ke/` scheduler**, chỉ phụ thuộc `ke/` spinlock/IRQL.
   Lý do: scheduler cấp stack bằng `mm/`; vòng phụ thuộc ngược lại sẽ làm
   không thể khởi tạo theo thứ tự.
4. **Driver không biết process/syscall tồn tại.** Driver nhận IRP chứa buffer
   *kernel*; việc copy từ/tới user là của tầng syscall (giống NT: driver
   `METHOD_BUFFERED` không đụng địa chỉ user).
5. **Mọi con trỏ từ user là không đáng tin** — chỉ đi qua `copy_from_user` /
   `copy_to_user` (mục 5.5.6), không bao giờ dereference trực tiếp.
6. **Code `arch/` không chứa chính sách** (policy): IDT biết cách *gọi*
   handler, không quyết định process nào bị kill.

### 2.3 Ánh xạ sang Windows NT

Mỗi module map vào một khái niệm có trong sách Windows Internals /
Windows Kernel Programming — đọc song song để so sánh.

| my-os | Windows NT tương ứng | Điểm giống | Điểm cố tình đơn giản hoá |
|---|---|---|---|
| `boot/` Stage2 + `BootInfo` | winload.efi + `LOADER_PARAMETER_BLOCK` | Loader dò RAM, nạp kernel + dữ liệu phụ, truyền 1 struct | BIOS thay UEFI, không có boot driver |
| `arch/x86_64` | HAL + `Ki*` trong ntoskrnl | Ẩn PIC/PIT/MSR sau API | Chỉ PIC 8259, chưa APIC |
| `struct Cpu` qua GS | `KPCR` / `KPRCB` (cũng qua GS trên x64) | `swapgs` khi vào/ra kernel | 1 CPU |
| `isr_common` / `interrupt_dispatch` | `KiInterruptDispatch`, `KINTERRUPT` | Stub asm → dispatch C | Không có interrupt object chain |
| `ke/` IRQL | `KeRaiseIrql`/`KeLowerIrql` | PASSIVE/DISPATCH/DEVICE/HIGH | Không có APC level thật |
| `ke/` DPC | `KeInsertQueueDpc` | ISR ngắn, việc nặng dồn sang DPC | 1 hàng đợi, không có DPC target CPU |
| `ke/` Event/Mutex/wait | Dispatcher objects, `KeWaitForSingleObject` | Signaled state + wait list | Không wait nhiều object |
| `ke/` scheduler | KTHREAD states, quantum, ready queue | Ready/Running/Waiting, quantum theo tick | Round-robin, chưa priority boost |
| `mm/` PMM bitmap | PFN database | Theo dõi frame vật lý | Bitmap 1 bit/frame (nâng cấp sau: mục 5.5.1) |
| `mm/` HHDM | (Windows không có direct map toàn RAM; dùng hyperspace/system PTE) | — | Chọn kiểu Linux vì đơn giản |
| `mm/` VMA list | VAD tree | Mô tả vùng nhớ ảo của process | Linked list thay AVL tree |
| `mm/` recursive map (tuỳ chọn) | `MiGetPteAddress`, self-map PML4 | Truy cập PTE qua địa chỉ ảo cố định | Bài học phụ, không làm nền |
| `mm/` `kmalloc(size, tag)` | `ExAllocatePool2(flags, size, tag)` | Pool tag để truy rò rỉ | 1 pool, không paged/nonpaged |
| `mm/` page fault ở IRQL ≥ DISPATCH → panic | Bugcheck `IRQL_NOT_LESS_OR_EQUAL` | Đúng cùng một luật | — |
| `copy_from_user` + exception table | `ProbeForRead` + `__try/__except` | Fault khi đọc user → trả lỗi, không crash | Không có SEH tổng quát |
| `ob/` | Object Manager, `ObReferenceObjectByHandle` | Header + refcount + handle table + access mask | Không có namespace `\Device\...` đầy đủ |
| `ps/` | EPROCESS/ETHREAD, `PspCreateProcess` | Process = address space + handle table; thread = đơn vị lập lịch | ELF thay PE |
| `io/` | I/O Manager, `DRIVER_OBJECT`, `DEVICE_OBJECT`, IRP, `IoCallDriver` | MajorFunction dispatch table, device stack | Đồng bộ trước, chưa `STATUS_PENDING` |
| `fs/` block cache | Cache Manager | Cache block đĩa | Theo block, chưa theo file view |
| `panic()` + backtrace | `KeBugCheckEx` | Dừng có chẩn đoán | Không crash dump |

### 2.4 Cây thư mục đích

```
my-os/
├── boot/
│   ├── boot.asm                 # Stage1 + Stage2 (E820, unreal-mode loader, paging, handoff)
│   └── bootinfo.inc             # offset BootInfo/manifest — khớp kernel/include/kernel/bootinfo.hpp
├── tools/
│   └── mkimage.py               # ghép os.img + ghi disk manifest (LBA, số sector kernel/initrd)
├── kernel/
│   ├── Makefile, linker.ld
│   ├── kernel_main.cpp          # CHỈ chứa thứ tự khởi tạo (mục 5.0)
│   ├── include/kernel/          # bootinfo.hpp, types.hpp, status.hpp, syscall_numbers.hpp (dùng chung với libc)
│   ├── lib/                     # kprintf, panic, string.asm, list.hpp, cxx_runtime.cpp, ktest.hpp
│   ├── arch/x86_64/
│   │   ├── entry.asm            # _start (thay kernel_entry.asm), kernel image header
│   │   ├── cpu.hpp / cpu.cpp    # Cpu per-CPU, CR/MSR/port I/O, cpuid
│   │   ├── gdt.*, gdt_flush.asm, tss.*
│   │   ├── idt.*, isr_stubs.asm, interrupts.cpp (dispatch), exceptions.cpp
│   │   ├── pic.*, pit.*
│   │   ├── context_switch.asm, syscall_entry.asm, user_copy.asm
│   │   └── paging.hpp           # bit PTE, invlpg, CR3
│   ├── ke/                      # irql.*, spinlock.hpp, dpc.*, timer.*, sched.*, wait.* (event/mutex)
│   ├── mm/                      # e820.*, pmm.*, vmm.*, address_space.*, heap.*, fault.cpp, user_copy.hpp
│   ├── ob/                      # object.*, handle_table.*
│   ├── ps/                      # process.*, thread.*, elf_loader.*, syscall.cpp (bảng + dispatch)
│   ├── io/                      # driver.* (DriverObject/DeviceObject/IRP), devfs.*
│   ├── drivers/                 # serial.*, keyboard.*, tty.*, vga_console.*, ata.*
│   └── fs/                      # vfs.*, path.cpp, tarfs.*, block_cache.*, fat32/
├── libc/                        # crt0.asm, syscall.asm, string.c, stdio.c (printf), stdlib.c (malloc)
├── userland/                    # init/, shell/, programs/
├── tests/                       # run_ktests.sh (QEMU headless + isa-debug-exit)
└── docs/
    ├── architecture.md          # file này
    └── notes/                   # ghi chú chi tiết từng cơ chế
```

---

## 3. Các luồng tương tác xuyên thành phần

> Đây là phần trả lời trực tiếp câu hỏi "các thành phần trong OS nói chuyện
> với nhau như thế nào". Mỗi luồng đi qua nhiều tầng; số trong ngoặc `[5.x]`
> trỏ tới phần thiết kế chi tiết.

### 3.1 Luồng khởi động (bật máy → process đầu tiên)

```
BIOS ──load 512B──► Stage1 @0x7C00 ──INT13h──► Stage2 @0x7E00                    [5.1]
Stage2 (Real Mode, IF=1 vì BIOS cần ngắt):
   ├─ đọc disk manifest (LBA 33)               ← biết kernel/initrd nằm ở đâu, dài bao nhiêu
   ├─ INT 15h E820 → 0x21000                   ← chỉ làm được ở Real Mode
   ├─ bật unreal mode → nạp kernel từng khối 64 sector → copy lên 0x100000
   ├─ đọc kernel header → biết mem_size (gồm .bss) → nạp initrd NGAY SAU mem_size
   ├─ ghi BootInfo @0x20000
   ├─ cli  ◄──────────────────────── sửa lỗi P0: từ đây tới khi PIC đã remap, IF luôn = 0
   ├─ lgdt, CR0.PE → Protected Mode
   ├─ page table: identity 1GB + HHDM 1GB + higher-half 1GB (dùng chung 1 PD)
   └─ PAE, EFER.LME, CR0.PG → Long Mode → mov rdi, HHDM+0x20000 ; jmp header.entry
_start (0xFFFFFFFF801xxxxx):  cli, cld, giữ RDI, zero .bss, dựng stack, call kernel_main(bootinfo)
kernel_main:                                                                       [5.0]
   serial → ctor global → GDT/TSS/per-CPU → IDT 256 + PIC remap&mask   (IRQL=HIGH)
   → PMM(BootInfo) → VMM (page table riêng, bỏ identity) → heap        (từ đây `new` dùng được)
   → ke: DPC, timer, scheduler, PIT 100Hz → ke_lower_irql(PASSIVE) → sti  (ngắt sống)
   → io + drivers → VFS mount initrd "/" → syscall MSR
   → ps_spawn("/bin/init") → thread boot trở thành idle thread: for(;;) hlt
```

**Điểm then chốt về tương tác:** mỗi mũi tên trong `kernel_main` là một phụ
thuộc cứng — heap cần VMM, VMM cần PMM, scheduler cần heap (cấp `Thread`) và
cần timer IRQ, process cần scheduler + VFS (đọc ELF) + ob (handle table).
Thứ tự này *chính là* đồ thị phụ thuộc của cả OS.

### 3.2 Luồng ngắt timer → preempt (chuyển thread không tự nguyện)

```
Thread A đang chạy ở ring 3 (hoặc ring 0, IRQL=PASSIVE)
   │ PIT phát IRQ0 → PIC master → CPU vector 32
   ▼
CPU: (ring3→0) nạp RSP từ TSS.RSP0; push SS,RSP,RFLAGS,CS,RIP; IF=0         [5.2.3]
isr_stub_32: push 0 (error giả), push 32 → isr_common
isr_common:  CS&3 ≠ 0 ? swapgs ; push 15 GPR ; cld ; call interrupt_dispatch(frame)
interrupt_dispatch:                                                           [5.2.4]
   cpu.irql = DEVICE
   irq_dispatch → pit_isr():  ticks++ ; timer_check() có timer hết hạn? → ke_queue_dpc(timer_dpc)
                              --A.quantum == 0 ? → cpu.need_resched = true
   pic_send_eoi(0)
   cpu.irql = PASSIVE (giá trị cũ)
   old < DISPATCH và (dpc_pending || need_resched):
        irql=DISPATCH; sti; ke_drain_dpcs()  → timer_dpc: ke_ready(thread B đang sleep) [5.4.3]
        cli; ke_schedule_locked()                                                   [5.4.5]
             pick_next → B ; TSS.RSP0 = B.stack_top ; cpu.kernel_rsp = B.stack_top
             B khác process? → mov cr3, B.process.pml4
             switch_context(&A.kernel_rsp, B.kernel_rsp)   ── A "đông cứng" tại đây
   ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─
B tiếp tục đúng chỗ B từng gọi switch_context (hoặc thread_trampoline nếu B mới)
   ...sau này, khi A được chọn lại: switch_context trả về trong interrupt_dispatch của A
   → return → isr_common pop GPR → swapgs (nếu về ring 3) → iretq → A chạy tiếp như chưa có gì
```

**Bài học tương tác:** ngắt (arch) chỉ *đặt cờ* và *xếp DPC*; quyết định
chuyển thread (ke) chỉ xảy ra ở "điểm an toàn" khi IRQL hạ xuống dưới
DISPATCH. Đây đúng là mô hình NT: ISR → DPC → dispatcher.

### 3.3 Luồng bàn phím → shell nhận một dòng lệnh

```
shell (ring 3): read(0, buf, 256)
  → libc syscall stub → SYSCALL → syscall_entry → syscall_dispatch → sys_read     [5.7.2]
  → ob_reference_by_handle(handle 0) → FileObject(/dev/tty0) → vnode->read()        [5.6]
  → tty_read(): chưa có dòng hoàn chỉnh → ke_event_wait(tty.line_ready)             [5.8.4]
       thread shell: state=Waiting, vào wait list → ke_schedule → CPU chạy idle (hlt)
  ...
Người dùng gõ 'l','s','\n' — với mỗi phím:
  IRQ1 → vector 33 → keyboard_isr(): sc = inb(0x60); ring_push(sc); ke_queue_dpc()  [5.8.3]
       (ISR KHÔNG dịch scancode, KHÔNG đánh thức thread — chỉ việc tối thiểu ở IRQL DEVICE)
  → interrupt_dispatch exit: drain DPC ở IRQL DISPATCH
  → keyboard_dpc(): scancode → ký tự → tty_input_char(): echo ra console, đưa vào line buffer
       gặp '\n' → ke_event_set(tty.line_ready) → ke_ready(shell) → need_resched
  → ke_schedule: idle → shell
tty_read() thức dậy: copy dòng vào kbuf → sys_read: copy_to_user(buf) → trả số byte
  → syscall_entry: pop, swapgs, sysret → shell có "ls\n"
```

**Bài học:** 4 ngữ cảnh thực thi khác nhau cùng tham gia một thao tác —
ISR (IRQL DEVICE), DPC (DISPATCH), thread kernel của shell (PASSIVE), code
user (ring 3). Mỗi chỗ chỉ được làm những việc hợp với IRQL của nó
(bảng 4.8).

### 3.4 Luồng `write(1, "hello\n", 6)` từ user

```
libc write() → __syscall6(SYS_WRITE, 1, ptr, 6)
SYSCALL: CPU lưu RIP→RCX, RFLAGS→R11, nạp CS=0x08, RIP=LSTAR, RFLAGS &= ~FMASK (IF=0)   [5.7.1]
syscall_entry: swapgs → lấy kernel RSP từ gs:[8] → push SyscallFrame → sti → call dispatch
syscall_dispatch: nr hợp lệ? → sys_write(1, ptr, 6)
sys_write:
   user_range_ok(ptr, 6)?                                   ← chặn con trỏ trỏ vào nửa kernel
   kbuf = kmalloc(6, 'Wrt ')                                ← mm/heap
   copy_from_user(kbuf, ptr, 6)  (rep movsb, có thể #PF → exception table → E_FAULT)  [5.5.6]
   f = ob_reference_by_handle(proc.handles, 1, File, WRITE) ← ob/ kiểm tra quyền + tăng refcount
   f->vnode->write(offset, kbuf, 6)                         ← fs/devfs → io/
       devfs vnode → Irp{Write, kbuf, 6} → io_call_driver(tty_device, irp)             [5.8.1]
       → tty driver MajorFunction[Write] → vga_console_write + serial_write
   ob_dereference(f); kfree(kbuf)
kiểm tra user_rip canonical → pop → swapgs → sysretq                                  [5.7.1]
```

### 3.5 Luồng page fault (3 kết cục)

```
CPU truy cập địa chỉ ảo X → MMU không có PTE hợp lệ → #PF (vector 14), CR2 = X, error code
isr_common → interrupt_dispatch → exception_dispatch → mm_page_fault(frame)           [5.5.5]
   X < USER_TOP và có process hiện tại?
   ├─ tìm VMA chứa X, quyền khớp error code, trang chưa present
   │     → pmm_alloc_frame → zero qua HHDM → vmm_map → return → iretq chạy lại lệnh gây lỗi
   │     (KẾT CỤC 1: demand paging — chương trình không biết gì đã xảy ra)
   ├─ không hợp lệ, fault từ ring 3 (error & 4)
   │     → ps_terminate_current(E_FAULT) → schedule sang thread khác
   │     (KẾT CỤC 2: process bị kill, kernel sống)
   └─ không hợp lệ, fault từ ring 0 trong copy_user_bytes (RIP có trong exception table)
         → frame->rip = địa chỉ fixup → hàm copy trả "số byte chưa copy" → syscall trả E_FAULT
   ngược lại (kernel tự truy cập sai, hoặc fault ở IRQL ≥ DISPATCH)
         → panic("page fault", frame) + backtrace                                        [5.3.3]
         (KẾT CỤC 3: bug kernel — tương đương bugcheck 0x50 / 0xA trên Windows)
   Đặc biệt: fault trúng guard page dưới kernel stack → handler cũng không còn stack → #DF
         → CPU nhảy IST1 → in "kernel stack overflow"
```

### 3.6 Luồng chạy `/disk/bin/ls` từ đĩa FAT32

```
shell: spawn("/disk/bin/ls") → SYSCALL → sys_spawn → copy_from_user(path)
ps_spawn:                                                                            [5.7.4]
  vfs_open(path):                                                                   [5.9.1]
     "/" (tarfs root) → lookup "disk" → vnode là mount point → nhảy sang root FAT32
     → FatVnode::lookup("bin") → đọc cluster thư mục:
           bcache_get(ata0, lba) ── miss ──► Irp{Read, lba} → io_call_driver(ata0)   [5.9.3]
                                              → ata_pio_read: outb 0x1F2..0x1F7, poll, insw [5.8.5]
     → lookup "ls" → FatVnode(file)
  đọc ELF header (FatVnode::read → FAT chain → bcache → ATA) → kiểm tra magic/arch
  vmm_create_address_space(): PML4 mới, copy entry 256..511 (nửa kernel dùng chung)  [5.5.3]
  mỗi PT_LOAD: vma_add + cấp frame + copy dữ liệu QUA HHDM (không cần đổi CR3)
  VMA stack user, Process{handles: kế thừa 0,1,2 từ shell}, Thread{trampoline user}
  ke_ready(thread) → sys_spawn trả pid
Lần schedule tới thread mới: CR3 = PML4 mới → user_thread_start → enter_user_mode(e_entry, rsp)
  → iretq với CS=0x2B, SS=0x23 → ring 3, `_start` của ls
```

**Bài học:** "chạy một chương trình" chạm vào gần như *mọi* thành phần —
đây là lý do nó nằm cuối lộ trình, và cũng là bài kiểm tra tích hợp tốt nhất.

---

## 4. Hợp đồng nền tảng — chốt trước khi code

> "Hợp đồng" = con số/layout mà ≥2 thành phần cùng phụ thuộc. Sai một bên là
> lỗi âm thầm. Mọi hợp đồng dưới đây phải có `static_assert` ở phía C++ và
> nằm trong bảng ràng buộc mục 8.

### 4.1 Bố cục đĩa + disk manifest

Thay hằng `KERNEL_SECTORS` nằm ở 2 nơi bằng **một sector manifest do build
tool ghi ra** — bootloader đọc manifest, không hardcode kích thước nữa.

```
LBA 0           Stage1 (MBR, 512B, chữ ký 0xAA55)
LBA 1..32       Stage2 (SECTORS_TO_LOAD = 32 — hằng số duy nhất còn lại giữa boot.asm và mkimage.py)
LBA 33          DiskManifest (512B)
LBA 34..        kernel.bin (đệm tới bội 512)
LBA k..         initrd.tar (USTAR, đệm tới bội 512)   — k = 34 + kernel_sectors
```

```cpp
// kernel/include/kernel/bootinfo.hpp  (và boot/bootinfo.inc khai báo offset tương ứng)
struct DiskManifest {
    uint64_t magic;            // 'MYOSDISK' — Stage2 dừng với thông báo lỗi nếu sai
    uint32_t version;          // = 1
    uint32_t kernel_lba;
    uint32_t kernel_sectors;
    uint32_t initrd_lba;
    uint32_t initrd_sectors;   // 0 = không có initrd
    uint32_t reserved;
    uint8_t  padding[512 - 32];
} __attribute__((packed));
static_assert(sizeof(DiskManifest) == 512);
```

### 4.2 Bố cục bộ nhớ vật lý lúc khởi động

| Vùng vật lý | Nội dung | Ai ghi | PMM |
|---|---|---|---|
| `0x00000`–`0x004FF` | IVT + BIOS Data Area | BIOS | khoá |
| `0x01000`–`0x04FFF` | Page table khởi động: PML4, PDPT_LOW, PDPT_HIGH, PD | Stage2 | khoá (<1MB); thu hồi được sau khi VMM đổi CR3 |
| `0x07C00`–`0x07DFF` | Stage1 | BIOS | khoá |
| `0x07E00`–`0x0BDFF` | Stage2 | Stage1 | khoá |
| `0x10000`–`0x17FFF` | Bounce buffer: mỗi lần `INT 13h` đọc ≤64 sector vào đây | Stage2 | khoá |
| `0x20000`–`0x20FFF` | `BootInfo` | Stage2 | khoá |
| `0x21000`–`0x2127F` | Mảng E820 (tối đa 32 × 20B) | Stage2 | khoá |
| `0x80000`–`0x8FFFF` | Stack Stage2 (đỉnh `0x90000`) | Stage2 | khoá |
| `0x9FC00`–`0xFFFFF` | EBDA, VGA, ROM | phần cứng | khoá |
| `0x100000`–`kernel_phys_end` | Kernel (.text → .bss) | Stage2 copy qua unreal mode | khoá |
| `align4K(kernel_phys_end)`–`+initrd_size` | initrd.tar | Stage2 | khoá (tarfs đọc tại chỗ) |

**Vì sao initrd đặt sau `kernel_phys_end` chứ không sau cuối file kernel:**
`.bss` không có trong file nhưng chiếm RAM ngay sau `.data`. Đặt initrd sát
cuối file thì `_start` zero `.bss` sẽ xoá mất initrd. Đó là lý do kernel header
(4.3) phải khai `mem_size`.

### 4.3 Kernel image header

8 byte đầu `kernel.bin` không còn là `_start` nữa — là một header (giống ý
tưởng Multiboot). Nhờ đó ràng buộc "`kernel_entry.o` phải link đầu tiên" biến mất.

```nasm
; kernel/arch/x86_64/entry.asm
section .boot_header                 ; linker.ld đặt section này ĐẦU TIÊN (KEEP)
kernel_image_header:
    dq 'MYOSKRNL'                    ; magic (NASM lưu 8 ký tự theo thứ tự byte)
    dq _start                        ; entry — địa chỉ ẢO higher-half
    dq 0x100000                      ; địa chỉ vật lý phải nạp vào
    dq __kernel_file_size            ; byte cần đọc từ đĩa (.text+.rodata+.data)
    dq __kernel_mem_size             ; byte chiếm trong RAM (gồm .bss) — xem 4.2
```

### 4.4 BootInfo (bootloader → kernel)

Phiên bản mini của `LOADER_PARAMETER_BLOCK`. Stage2 ghi tại vật lý `0x20000`,
truyền **địa chỉ ảo HHDM** của nó qua `RDI` (địa chỉ này còn hợp lệ sau khi
kernel bỏ identity map).

```cpp
constexpr uint64_t BOOTINFO_MAGIC = 0x544F4F42534F594DULL;   // 'MYOSBOOT' little-endian

struct BootInfo {
    uint64_t magic;                  // +0
    uint32_t version;                // +8   = 1
    uint32_t e820_count;             // +12
    uint64_t e820_phys;              // +16  mảng E820Entry 20 byte (layout cũ, giữ nguyên)
    uint64_t kernel_phys_start;      // +24  = 0x100000
    uint64_t kernel_phys_end;        // +32  = start + header.mem_size
    uint64_t initrd_phys;            // +40  0 nếu không có
    uint64_t initrd_size;            // +48  byte
    uint64_t boot_page_tables_phys;  // +56  = 0x1000, 4 trang
    uint8_t  boot_drive;             // +64
    uint8_t  reserved[7];            // +65
} __attribute__((packed));
static_assert(sizeof(BootInfo) == 72);
static_assert(offsetof(BootInfo, initrd_phys) == 40);   // khớp BOOTINFO_INITRD_PHYS trong bootinfo.inc
```

### 4.5 Không gian địa chỉ ảo

Chốt **trước** dòng VMM đầu tiên. Mỗi vùng kernel chiếm trọn ≥1 entry PML4
để dễ cấp sẵn và dễ nhận biết khi debug (nhìn địa chỉ là biết thuộc vùng nào).

| Vùng | Bắt đầu | PML4 idx | Kích thước | Ghi chú |
|---|---|---|---|---|
| Trang null | `0x0000000000000000` | 0 | 4MB đầu | Không bao giờ map → bắt null pointer (cả user lẫn kernel) |
| User space | `0x0000000000400000` | 0–255 | tới `USER_TOP = 0x00007FFFFFFFF000` | Riêng từng process; bỏ trang cuối trước lỗ non-canonical |
| — lỗ non-canonical — | `0x0000800000000000` | — | — | CPU #GP nếu truy cập |
| Direct map (HHDM) | `0xFFFF800000000000` | 256 | = RAM vật lý (≤4GB lúc đầu) | `phys_to_virt(p) = p + HHDM_BASE`; page 2MB; RW + NX |
| Kernel heap | `0xFFFFC00000000000` | 384 | 512GB ảo | Ảo liên tục, frame rời rạc |
| Kernel stacks | `0xFFFFE00000000000` | 448 | 512GB ảo | Mỗi stack = 1 guard page (không map) + N trang |
| MMIO | `0xFFFFF00000000000` | 480 | 512GB ảo | LAPIC/IOAPIC/framebuffer (sau này) |
| Recursive map (tuỳ chọn, bài học) | `0xFFFFFF0000000000` | 510 | 512GB | PML4[510] trỏ về chính PML4 |
| Kernel image | `0xFFFFFFFF80000000` | 511 (PDPT 510) | 2GB | Khớp `-mcmodel=kernel`; `.text` RX, `.rodata` R, `.data/.bss` RW+NX |

```cpp
// kernel/include/kernel/types.hpp
using PhysAddr = uint64_t;
using VirtAddr = uint64_t;
constexpr uint64_t PAGE_SIZE      = 4096;
constexpr VirtAddr USER_BASE      = 0x0000000000400000ULL;
constexpr VirtAddr USER_TOP       = 0x00007FFFFFFFF000ULL;
constexpr VirtAddr HHDM_BASE      = 0xFFFF800000000000ULL;
constexpr VirtAddr KHEAP_BASE     = 0xFFFFC00000000000ULL;
constexpr VirtAddr KHEAP_LIMIT    = 0xFFFFC08000000000ULL;
constexpr VirtAddr KSTACK_BASE    = 0xFFFFE00000000000ULL;
constexpr VirtAddr KSTACK_LIMIT   = 0xFFFFE08000000000ULL;
constexpr VirtAddr MMIO_BASE      = 0xFFFFF00000000000ULL;
constexpr VirtAddr KERNEL_VMA     = 0xFFFFFFFF80000000ULL;

inline VirtAddr phys_to_virt(PhysAddr p) { return p + HHDM_BASE; }
inline bool is_user_range(uint64_t addr, uint64_t len) {
    return addr <= USER_TOP && len <= USER_TOP - addr;   // viết kiểu này để không tràn số khi addr+len
}
```

**Vì sao nửa cao dùng chung mọi process:** khi tạo address space mới chỉ cần
copy entry PML4 256–511 (con trỏ tới *cùng* các PDPT). Vì tất cả PDPT kernel
được cấp sẵn lúc boot, về sau thêm mapping kernel chỉ sửa PDPT/PD/PT bên
dưới — mọi process thấy ngay, **không bao giờ phải đồng bộ PML4**.

### 4.6 GDT (bố cục cuối cùng, sẵn sàng SYSCALL/SYSRET)

| Selector | Entry | Access | Flags | Dùng cho |
|---|---|---|---|---|
| `0x00` | null | — | — | bắt buộc |
| `0x08` | kernel code64 | `0x9A` | `0xA0` (G, L) | CS ring 0; `STAR[47:32]` |
| `0x10` | kernel data | `0x92` | `0xC0` | SS ring 0 (SYSCALL nạp `STAR[47:32]+8`) |
| `0x18` | user code32 (giữ chỗ) | `0xFA` | `0xC0` | `STAR[63:48]` — không dùng thật |
| `0x20` | user data | `0xF2` | `0xC0` | SS ring 3 = `0x23` (SYSRET: base+8) |
| `0x28` | user code64 | `0xFA` | `0xA0` | CS ring 3 = `0x2B` (SYSRET: base+16) |
| `0x30` | TSS (16B, chiếm 2 slot) | `0x89` | — | `ltr 0x30` |

### 4.7 IDT

| Vector | Nguồn | Stub | IST |
|---|---|---|---|
| 0–31 | CPU exception | error code thật cho 8, 10–14, 17, **21** (vector 30 #SX chỉ có trên AMD và có error code — hiện để `NOERR` vì QEMU TCG không phát; nếu chạy KVM trên AMD thì đổi) | #DF=1, NMI(2)=2, #MC(18)=3, còn lại 0 |
| 32–47 | PIC IRQ0–15 (sau remap) | không error code | 0 |
| 48–254 | chưa dùng | stub mặc định → log "unexpected vector N" | 0 |
| 255 | spurious APIC (sau này) | bỏ qua | 0 |

Vector có IST (2, 8, 18) dùng **stub "paranoid"** (mục 5.2.3) vì chúng có thể
tới lúc GS đang ở trạng thái user dù CS là kernel.

### 4.8 IRQL và luật đồng thời

Mô hình rút gọn của NT, cho 1 CPU + PIC 8259:

| IRQL | Giá trị | Ai chạy ở đây | ĐƯỢC làm | CẤM làm |
|---|---|---|---|---|
| `PASSIVE` | 0 | thread (kernel/syscall) | block (wait event/mutex), page fault trên bộ nhớ user, `kmalloc`, mọi thứ | — |
| `DISPATCH` | 2 | DPC, code giữ `SpinLock`, scheduler | `kmalloc`/`kfree`, `ke_event_set`, `ke_ready`, `SpinLock` | block, sleep, `KMutex`, chạm bộ nhớ user (có thể #PF) |
| `DEVICE` | 3 | ISR | port I/O, ghi ring buffer, `ke_queue_dpc`, đặt cờ | `kmalloc`, `ke_event_set`, `SpinLock` thường, mọi thứ có thể chờ |
| `HIGH` | 15 | early boot, `panic` | chỉ code tự đủ | gần như mọi thứ |

**Hiện thực trên PIC:** bất biến *IF = 0 ⇔ IRQL ≥ DEVICE* (sau khi boot xong).
Nâng lên `DISPATCH` chỉ ghi `cpu.irql` — không đụng phần cứng; DPC và
preempt tự động bị hoãn vì chỉ được xử lý khi IRQL hạ xuống < `DISPATCH`.

**Ba loại khoá, thứ tự lấy cố định** (lấy ngược thứ tự = deadlock):

```
KMutex (PASSIVE, có thể ngủ)  →  SpinLock (nâng DISPATCH)  →  IrqSpinLock (nâng HIGH, cli)
```

- `KMutex`: thao tác dài — VFS, buffer cache, thiết bị ATA.
- `SpinLock`: dữ liệu dùng chung giữa thread và DPC — heap, PMM, ready queue, handle table.
- `IrqSpinLock`: dữ liệu dùng chung với ISR — hàng đợi DPC.

**Bất biến context switch** (mục 5.4.5): `switch_context` *chỉ* được gọi ở
IRQL = `DISPATCH` và IF = 0. Mọi đường vào scheduler đều phải thoả điều này.

### 4.9 Mã trạng thái

```cpp
// kernel/include/kernel/status.hpp — dùng chung kernel + libc (syscall trả số âm)
using Status = int64_t;
constexpr Status OK          =  0;
constexpr Status E_NOMEM     = -1;
constexpr Status E_FAULT     = -2;   // con trỏ user không hợp lệ
constexpr Status E_INVAL     = -3;
constexpr Status E_NOENT     = -4;
constexpr Status E_BADF      = -5;   // handle không hợp lệ / sai loại
constexpr Status E_ACCESS    = -6;   // handle không có quyền yêu cầu
constexpr Status E_NOSYS     = -7;
constexpr Status E_IO        = -8;
constexpr Status E_NOTDIR    = -9;
constexpr Status E_ISDIR     = -10;
constexpr Status E_NOEXEC    = -11;  // file không phải ELF hợp lệ
constexpr Status E_BUSY      = -12;
```

---

## 5. Thiết kế chi tiết từng tầng

### 5.0 `kernel_main` — thứ tự khởi tạo

`kernel_main.cpp` chỉ còn làm một việc: gọi `*_init()` theo đúng đồ thị phụ
thuộc. Mỗi dòng in log qua serial để khi treo biết treo ở bước nào.

```cpp
// kernel/kernel_main.cpp
extern "C" [[noreturn]] void kernel_main(BootInfo* boot) {
    // ── Giai đoạn 1: IRQL = HIGH, IF = 0, chưa có heap ─────────────────────
    serial_init();                       // log sớm nhất có thể (không cần gì khác)
    kprintf("my-os booting\n");
    KASSERT(boot->magic == BOOTINFO_MAGIC, "BootInfo sai magic — bootloader/kernel lệch phiên bản");
    run_global_constructors();           // .init_array — ctor KHÔNG được dùng heap/ngắt

    cpu_init_bsp();                      // GDT (4.6) + TSS (IST1..3) + GS_BASE = &cpu0
    idt_init();                          // 256 vector
    pic_remap_and_mask_all();            // IRQ → 32..47, mask hết

    // ── Giai đoạn 2: bộ nhớ ────────────────────────────────────────────────
    pmm_init(*boot);                     // bitmap + khoá kernel/initrd/<1MB
    vmm_init(*boot);                     // page table riêng: HHDM toàn RAM, kernel W^X, cấp sẵn PML4[256..511]
                                         // → nạp CR3 mới, bật EFER.NXE + CR0.WP, bỏ identity map
    heap_init();                         // từ đây `new`/`kmalloc` dùng được

    // ── Giai đoạn 3: ngắt sống + lập lịch ─────────────────────────────────
    dpc_init();
    timer_init();
    sched_init();                        // biến luồng đang chạy thành "thread 0" (sau thành idle)
    pit_init(100);                       // 100Hz, đăng ký IRQ0 → unmask
    ke_lower_irql(Irql::Passive);        // ★ lần đầu tiên IF = 1 kể từ Stage2

    // ── Giai đoạn 4: I/O, filesystem, user mode ─────────────────────────────
    io_init();
    serial_enable_irq();
    keyboard_init();                     // IRQ1
    tty_init();                          // /dev/tty0 = keyboard + vga + serial
    ata_init();                          // M7
    vfs_init();
    tarfs_mount_root(boot->initrd_phys, boot->initrd_size);
    devfs_mount("/dev");
    syscall_init();                      // STAR/LSTAR/FMASK, EFER.SCE

    Process* init = nullptr;
    Status s = ps_spawn("/bin/init", nullptr, &init);
    if (s != OK) panic("khong chay duoc /bin/init: status=%ld", s);

    sched_become_idle();                 // không return: for (;;) { sti; hlt; }
}
```

### 5.1 Boot (`boot/boot.asm`)

#### 5.1.1 Stage2 — luồng mới

```
stage2_start:                       (Real Mode, IF=1)
  1. đọc DiskManifest (LBA 33) → bounce buffer, kiểm tra magic
  2. detect_memory_e820 → 0x21000                 (giữ nguyên code hiện tại, đổi đích)
  3. enter_unreal
  4. load_image(kernel_lba, kernel_sectors, đích 0x100000)
  5. kiểm tra kernel header: magic, file_size ≤ kernel_sectors*512
  6. initrd_phys = align_up(0x100000 + mem_size, 0x1000)
     load_image(initrd_lba, initrd_sectors, đích initrd_phys)
  7. fill_bootinfo @0x20000
  8. cli                                             ← SỬA P0
  9. bật A20, lgdt, CR0.PE, far jmp → 32-bit
 10. setup_page_tables (5.1.3), PAE, LME, PG, far jmp → 64-bit
 11. mov rdi, HHDM_BASE + 0x20000 ; mov rax, [header.entry] ; jmp rax
```

#### 5.1.2 Unreal mode + nạp theo khối

**Vì sao:** Real Mode chỉ địa chỉ hoá được < 1MB, nhưng BIOS `INT 13h` chỉ
gọi được ở Real Mode. Unreal mode là mẹo: vào Protected Mode *một thoáng* chỉ
để nạp DS/ES với descriptor 4GB, rồi quay về Real Mode. CPU giữ nguyên
"descriptor cache" (limit 4GB) nên lệnh có tiền tố `a32` với EDI 32-bit ghi
được lên trên 1MB — trong khi vẫn gọi BIOS được.

```nasm
; Vào unreal mode: DS/ES giữ giá trị segment 0 nhưng limit ẩn = 4GB
enter_unreal:
    cli                          ; không để ngắt tới khi đang ở PM mà IDTR còn là IVT
    push ds
    push es
    lgdt [gdt_descriptor]
    mov eax, cr0
    or al, 1
    mov cr0, eax
    jmp short .pm                ; xả prefetch queue
.pm:
    mov bx, DATA_SEG32           ; nạp descriptor flat 4GB vào cache của DS/ES
    mov ds, bx
    mov es, bx
    and al, 0xFE
    mov cr0, eax                 ; về Real Mode — cache limit vẫn là 4GB
    pop es                       ; base trở lại 0 (segment 0), limit giữ nguyên
    pop ds
    sti                          ; BIOS INT 13h tiếp theo cần ngắt
    ret

; DAP dùng lại cho mọi lần đọc — local label để điền từng field theo tên
dap_io:
    db 0x10, 0
.count:   dw 0
.offset:  dw 0
.segment: dw 0
.lba:     dq 0

; load_image — nạp `sectors` sector từ `lba` lên địa chỉ vật lý 32-bit `dest`
; vào: EAX = lba, ECX = sectors, EDI = dest
BOUNCE_SEG     equ 0x1000        ; vật lý 0x10000
CHUNK_SECTORS  equ 64            ; < 127 (giới hạn EDD), 64*512 = 32KB vừa bounce buffer
load_image:
.next_chunk:
    test ecx, ecx
    jz .done
    mov ebx, ecx
    cmp ebx, CHUNK_SECTORS
    jbe .count_ok
    mov ebx, CHUNK_SECTORS
.count_ok:
    ; điền DAP: count = ebx, buffer = BOUNCE_SEG:0, lba = eax
    mov [dap_io.count], bx
    mov word [dap_io.offset], 0
    mov word [dap_io.segment], BOUNCE_SEG
    mov [dap_io.lba], eax
    mov dword [dap_io.lba + 4], 0

    push eax
    push ecx
    push edi
    mov si, dap_io
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    pop edi
    pop ecx
    pop eax
    jc disk_error

    ; copy ebx*512 byte từ 0x10000 lên [edi] — cần unreal mode (edi có thể > 1MB)
    push ecx
    push eax
    mov esi, 0x10000
    mov ecx, ebx
    shl ecx, 9                   ; × 512
    cld
    a32 rep movsb                ; dùng ESI/EDI/ECX 32-bit; EDI tự tăng tới khối tiếp theo
    pop eax
    pop ecx

    add eax, ebx                 ; lba += count
    sub ecx, ebx                 ; còn lại -= count
    jmp .next_chunk
.done:
    ret
```

> ⚠️ Kiểm chứng cần làm: một số BIOS reload DS/ES bên trong `INT 13h`. Trên
> SeaBIOS (QEMU) mẹo này hoạt động rộng rãi, nhưng phải xác nhận bằng cách
> nạp kernel > 64KB rồi so checksum (M0).

#### 5.1.3 Page table khởi động: 3 view của cùng 1GB vật lý

```nasm
PML4_ADDR equ 0x1000
PDPT_LOW  equ 0x2000
PDPT_HIGH equ 0x3000
PD_ADDR   equ 0x4000

setup_page_tables:
    mov edi, PML4_ADDR
    xor eax, eax
    mov ecx, 0x4000 / 4
    cld
    rep stosd                                          ; xoá 4 trang

    ; Cả identity và HHDM đều trỏ về PDPT_LOW → cùng PD → cùng 1GB vật lý đầu.
    mov dword [PML4_ADDR + 0   * 8], PDPT_LOW  | 0b11  ; 0x0000000000000000 (tạm, bỏ ở vmm_init)
    mov dword [PML4_ADDR + 256 * 8], PDPT_LOW  | 0b11  ; 0xFFFF800000000000 HHDM (tạm 1GB)
    mov dword [PML4_ADDR + 511 * 8], PDPT_HIGH | 0b11  ; 0xFFFFFF8000000000
    mov dword [PDPT_LOW  + 0   * 8], PD_ADDR   | 0b11
    mov dword [PDPT_HIGH + 510 * 8], PD_ADDR   | 0b11  ; 0xFFFFFFFF80000000 → vật lý 0

    mov edi, PD_ADDR                                   ; 512 × 2MB, giống code hiện tại
    mov eax, 0b10000011
    mov ecx, 512
.fill_pd:
    mov [edi], eax
    add eax, 0x200000
    add edi, 8
    loop .fill_pd
    ret
```

Kiểm tra chỉ số: `0xFFFFFFFF80100000 >> 39 & 0x1FF = 511`, `>> 30 & 0x1FF = 510`,
`>> 21 & 0x1FF = 0` → PD[0] = vật lý `0x000000`–`0x1FFFFF` → offset `0x100000` ✓.

> Chú ý: identity và HHDM dùng chung PDPT_LOW nên tạm thời xuất hiện thêm
> vài alias vô hại (vd PML4[0]/PDPT[510]). Chúng biến mất khi `vmm_init`
> nạp page table riêng của kernel.

#### 5.1.4 Handoff sang kernel

```nasm
BITS 64
long_mode_entry:
    xor eax, eax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov rsp, 0x90000
    mov rdi, 0xFFFF800000020000              ; BootInfo qua HHDM (4.4)
    mov rax, [0x100000 + 8]                  ; kernel_image_header.entry (4.3)
    jmp rax
```

### 5.2 `arch/x86_64` (≈ HAL)

#### 5.2.1 `cpu.hpp` — thanh ghi, MSR, port I/O, per-CPU

```cpp
// kernel/arch/x86_64/cpu.hpp
#pragma once
#include <cstdint>
#include <cstddef>

// ---- Port I/O ----
inline uint8_t inb(uint16_t port) {
    uint8_t v;
    asm volatile("in %1, %0" : "=a"(v) : "d"(port));   // AT&T: in %dx, %al
    return v;
}
inline void outb(uint16_t port, uint8_t value) {
    asm volatile("out %1, %0" : : "d"(port), "a"(value)); // AT&T: out %al, %dx
}
inline void insw(uint16_t port, uint16_t* buf, size_t count) {
    // `rep insw` làm thay đổi RDI và RCX → phải khai chúng là output ("=D", "=c"),
    // nếu chỉ khai là input, GCC được phép giả định chúng giữ nguyên giá trị.
    uint16_t* rdi_after;
    size_t rcx_after;
    asm volatile("rep insw" : "=D"(rdi_after), "=c"(rcx_after) : "d"(port), "D"(buf), "c"(count));
    (void)rdi_after;
    (void)rcx_after;
}
inline void io_wait() { outb(0x80, 0); }   // port 0x80 (POST code) — trễ ~1µs cho PIC đời cũ

// ---- MSR ----
constexpr uint32_t MSR_EFER           = 0xC0000080;
constexpr uint32_t MSR_STAR           = 0xC0000081;
constexpr uint32_t MSR_LSTAR          = 0xC0000082;
constexpr uint32_t MSR_FMASK          = 0xC0000084;
constexpr uint32_t MSR_GS_BASE        = 0xC0000101;
constexpr uint32_t MSR_KERNEL_GS_BASE = 0xC0000102;
constexpr uint64_t EFER_SCE = 1ULL << 0;
constexpr uint64_t EFER_NXE = 1ULL << 11;

inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return (static_cast<uint64_t>(hi) << 32) | lo;
}
inline void wrmsr(uint32_t msr, uint64_t v) {
    asm volatile("wrmsr" : : "c"(msr), "a"(static_cast<uint32_t>(v)), "d"(static_cast<uint32_t>(v >> 32)));
}

// ---- Control register & cờ ngắt ----
inline uint64_t read_cr0() { uint64_t v; asm volatile("mov %%cr0, %0" : "=r"(v)); return v; }
inline void write_cr0(uint64_t v) { asm volatile("mov %0, %%cr0" : : "r"(v)); }
inline uint64_t read_cr2() { uint64_t v; asm volatile("mov %%cr2, %0" : "=r"(v)); return v; }
inline uint64_t read_cr3() { uint64_t v; asm volatile("mov %%cr3, %0" : "=r"(v)); return v; }
inline void write_cr3(uint64_t v) { asm volatile("mov %0, %%cr3" : : "r"(v)); }
inline void invlpg(uint64_t va) { asm volatile("invlpg %0" : : "m"(*reinterpret_cast<const char*>(va))); }

constexpr uint64_t RFLAGS_IF = 1ULL << 9;
inline void cpu_disable_interrupts() { asm volatile("cli"); }
inline void cpu_enable_interrupts()  { asm volatile("sti"); }
inline uint64_t cpu_save_flags_and_cli() {
    uint64_t f;
    asm volatile("pushf; pop %0; cli" : "=r"(f));
    return f;
}
inline void cpu_restore_flags(uint64_t f) { if (f & RFLAGS_IF) cpu_enable_interrupts(); }

// ---- Per-CPU (≈ KPCR). GS_BASE trỏ vào đây khi ở kernel. ----
enum class Irql : uint8_t { Passive = 0, Dispatch = 2, Device = 3, High = 15 };
struct Thread;

struct Cpu {
    Cpu*     self;          // gs:0  — this_cpu() đọc ô này
    uint64_t kernel_rsp;    // gs:8  — đỉnh kernel stack thread hiện tại (syscall_entry dùng)
    uint64_t user_rsp;      // gs:16 — chỗ cất tạm RSP user trong syscall_entry
    Thread*  current;       // gs:24
    Irql     irql;
    bool     need_resched;
    bool     dpc_pending;
    uint64_t ticks;
};
// Offset dưới đây được hardcode trong syscall_entry.asm — đổi struct phải sửa asm.
static_assert(offsetof(Cpu, self) == 0);
static_assert(offsetof(Cpu, kernel_rsp) == 8);
static_assert(offsetof(Cpu, user_rsp) == 16);
static_assert(offsetof(Cpu, current) == 24);

inline Cpu& this_cpu() {
    Cpu* c;
    asm volatile("mov %%gs:0, %0" : "=r"(c));
    return *c;
}

// ---- CPUID ----
inline void cpuid(uint32_t leaf, uint32_t* a, uint32_t* b, uint32_t* c, uint32_t* d) {
    asm volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(0u));
}
inline bool cpu_has_nx() {
    uint32_t a, b, c, d;
    cpuid(0x80000001, &a, &b, &c, &d);
    return (d >> 20) & 1;
}
```

```cpp
// kernel/arch/x86_64/cpu.cpp
namespace {
// Khởi tạo hằng (constant initialization) → nằm sẵn trong .data, không cần constructor.
// IRQL bắt đầu ở HIGH: từ lúc boot tới ke_lower_irql(PASSIVE) trong kernel_main, IF luôn = 0.
Cpu cpu0 = {&cpu0, 0, 0, nullptr, Irql::High, false, false, 0};
}

void cpu_init_bsp() {
    gdt_init();                                   // layout 4.6 + ltr
    // GS_BASE = kernel, KERNEL_GS_BASE = user (0). `swapgs` hoán đổi hai giá trị này.
    // Đặt trước mọi SpinLock: SpinLock::acquire → ke_raise_irql → this_cpu() đọc gs:0.
    wrmsr(MSR_GS_BASE, reinterpret_cast<uint64_t>(&cpu0));
    wrmsr(MSR_KERNEL_GS_BASE, 0);
}
```

```cpp
// kernel/include/kernel/types.hpp (bổ sung)
constexpr uint64_t align_down(uint64_t v, uint64_t a) { return v & ~(a - 1); }        // a là lũy thừa 2
constexpr uint64_t align_up(uint64_t v, uint64_t a)   { return (v + a - 1) & ~(a - 1); }
```

#### 5.2.2 GDT + TSS

Giữ nguyên cấu trúc `gdt.cpp`/`tss.cpp` hiện tại, đổi layout theo 4.6 và thêm IST:

```cpp
// gdt.cpp — GdtEntry gdt[8]: 6 slot thường + 2 slot cho TSS 16 byte
set_entry(0, 0, 0,       0x00, 0x00);   // null
set_entry(1, 0, 0xFFFFF, 0x9A, 0xA0);   // 0x08 kernel code64
set_entry(2, 0, 0xFFFFF, 0x92, 0xC0);   // 0x10 kernel data
set_entry(3, 0, 0xFFFFF, 0xFA, 0xC0);   // 0x18 user code32 — chỉ giữ chỗ cho công thức SYSRET
set_entry(4, 0, 0xFFFFF, 0xF2, 0xC0);   // 0x20 user data   → SS ring 3 = 0x23
set_entry(5, 0, 0xFFFFF, 0xFA, 0xA0);   // 0x28 user code64 → CS ring 3 = 0x2B
tss_install_descriptor(reinterpret_cast<TssDescriptor*>(&gdt[6]));   // 0x30

// gdt.hpp
constexpr uint16_t KERNEL_CODE_SELECTOR = 0x08;
constexpr uint16_t KERNEL_DATA_SELECTOR = 0x10;
constexpr uint16_t USER_BASE_SELECTOR   = 0x18;   // ghi vào STAR[63:48]
constexpr uint16_t USER_DATA_SELECTOR   = 0x20 | 3;
constexpr uint16_t USER_CODE_SELECTOR   = 0x28 | 3;
constexpr uint16_t TSS_SELECTOR         = 0x30;
```

```cpp
// tss.cpp — 3 stack IST riêng + API cập nhật RSP0 cho scheduler
constexpr size_t IST_STACK_SIZE = 16384;
alignas(16) uint8_t ist_df_stack[IST_STACK_SIZE];    // IST1: #DF
alignas(16) uint8_t ist_nmi_stack[IST_STACK_SIZE];   // IST2: NMI
alignas(16) uint8_t ist_mc_stack[IST_STACK_SIZE];    // IST3: #MC

tss.ist1 = reinterpret_cast<uint64_t>(ist_df_stack  + IST_STACK_SIZE);
tss.ist2 = reinterpret_cast<uint64_t>(ist_nmi_stack + IST_STACK_SIZE);
tss.ist3 = reinterpret_cast<uint64_t>(ist_mc_stack  + IST_STACK_SIZE);

void tss_set_rsp0(uint64_t rsp0) { tss.rsp0 = rsp0; }   // scheduler gọi mỗi lần đổi thread
```

> Hạn chế đã biết của IST: nếu NMI lồng NMI trên cùng stack IST, frame đầu bị
> ghi đè (Linux phải xử lý rất phức tạp). Với 1 CPU trên QEMU, chấp nhận và
> ghi chú lại.

#### 5.2.3 `InterruptFrame` + stub

```cpp
// idt.hpp — thay struct Registers
struct InterruptFrame {
    // isr_common push (push sau nằm địa chỉ thấp hơn → field đầu = push cuối)
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector;
    uint64_t error_code;       // thật, hoặc 0 do stub push giả
    // CPU push — LUÔN đủ 5 ô ở Long Mode (Intel SDM §6.14.2), kể cả ring 0 → ring 0
    uint64_t rip, cs, rflags, rsp, ss;
};
static_assert(sizeof(InterruptFrame) == 22 * 8);
inline bool frame_from_user(const InterruptFrame* f) { return (f->cs & 3) != 0; }
```

```nasm
; isr_stubs.asm
%macro ISR_NOERR 1
global isr_stub_%1
isr_stub_%1:
    push qword 0
    push qword %1
    jmp isr_common
%endmacro

%macro ISR_ERR 1
global isr_stub_%1
isr_stub_%1:
    push qword %1
    jmp isr_common
%endmacro

%macro ISR_PARANOID_NOERR 1           ; vector chạy trên IST (NMI, #MC)
global isr_stub_%1
isr_stub_%1:
    push qword 0
    push qword %1
    jmp isr_paranoid_common
%endmacro

%macro ISR_PARANOID_ERR 1             ; #DF có error code (luôn = 0)
global isr_stub_%1
isr_stub_%1:
    push qword %1
    jmp isr_paranoid_common
%endmacro

ISR_NOERR 0
ISR_NOERR 1
ISR_PARANOID_NOERR 2                  ; NMI
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_PARANOID_ERR 8                    ; #DF
ISR_NOERR 9
ISR_ERR 10
ISR_ERR 11
ISR_ERR 12
ISR_ERR 13
ISR_ERR 14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR 17
ISR_PARANOID_NOERR 18                 ; #MC
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR 21                            ; #CP — CÓ error code (sửa lỗi P1-a)
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

; Vector 32..255: không error code. Dùng đúng mẫu `%+i` giống isr_stub_table hiện có.
%assign i 32
%rep 224
global isr_stub_%+i
isr_stub_%+i:
    push qword 0
    push qword i
    jmp isr_common
%assign i i+1
%endrep

; ----------------------------------------------------------------------------
; Stack khi vào isr_common: [rsp]=vector [rsp+8]=error [rsp+16]=rip [rsp+24]=cs
; ----------------------------------------------------------------------------
isr_common:
    test qword [rsp + 24], 3          ; tới từ ring 3?
    jz .gs_ok
    swapgs                            ; GS_BASE: user → kernel
.gs_ok:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    cld                               ; SysV: DF phải = 0 khi vào hàm C++ (sửa P1-d)
    mov rdi, rsp
    call interrupt_dispatch
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16                       ; bỏ vector + error_code
    test qword [rsp + 8], 3           ; giờ [rsp]=rip [rsp+8]=cs
    jz .ret
    swapgs                            ; về ring 3: GS_BASE kernel → user
.ret:
    iretq

; ----------------------------------------------------------------------------
; Paranoid: NMI/#MC/#DF có thể tới ĐÚNG lúc syscall_entry chưa kịp swapgs
; (CS = kernel nhưng GS vẫn là user). Không tin CS — đọc thẳng MSR GS_BASE:
; địa chỉ nửa cao (bit 63 = 1) nghĩa là GS đã là kernel.
; ----------------------------------------------------------------------------
isr_paranoid_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov ecx, 0xC0000101               ; MSR_GS_BASE
    rdmsr                             ; edx:eax (rax/rcx/rdx đã được lưu ở trên)
    xor ebx, ebx
    test edx, edx
    js .kernel_gs                     ; bit 63 = 1 → đã là GS kernel
    swapgs
    mov ebx, 1                        ; rbx là callee-saved → còn nguyên sau call
.kernel_gs:
    cld
    mov rdi, rsp
    call interrupt_dispatch
    test ebx, ebx
    jz .no_swap
    swapgs
.no_swap:
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16
    iretq

section .data
global isr_stub_table
isr_stub_table:
%assign i 0
%rep 256
    dq isr_stub_%+i
%assign i i+1
%endrep
```

#### 5.2.4 Interrupt dispatch

```cpp
// kernel/arch/x86_64/interrupts.cpp
constexpr uint8_t IRQ_BASE_VECTOR = 32;

extern "C" void interrupt_dispatch(InterruptFrame* f) {
    if (f->vector < 32) {                         // exception: giữ nguyên IRQL của ngữ cảnh bị ngắt
        exception_dispatch(f);
        return;
    }
    if (f->vector >= IRQ_BASE_VECTOR + 16) {
        kprintf("unexpected interrupt vector %lu\n", f->vector);
        return;
    }

    Cpu& cpu = this_cpu();
    const Irql old = cpu.irql;
    cpu.irql = Irql::Device;
    irq_dispatch(static_cast<uint8_t>(f->vector - IRQ_BASE_VECTOR), f);   // gọi ISR driver + EOI
    cpu.irql = old;

    // Điểm an toàn: ngữ cảnh bị ngắt đang ở IRQL < DISPATCH → xử lý việc bị hoãn.
    if (old < Irql::Dispatch && (cpu.dpc_pending || cpu.need_resched)) {
        cpu.irql = Irql::Dispatch;
        if (cpu.dpc_pending) {
            cpu_enable_interrupts();              // DPC chạy với ngắt bật (IRQL DISPATCH chặn preempt, không chặn IRQ)
            dpc_drain();
            cpu_disable_interrupts();
        }
        if (cpu.need_resched) {
            sched_schedule_locked();              // bất biến 4.8: IRQL=DISPATCH, IF=0
        }
        cpu.irql = old;
    }
}
```

```cpp
// kernel/arch/x86_64/exceptions.cpp
void exception_dispatch(InterruptFrame* f) {
    switch (f->vector) {
    case 14:
        mm_page_fault(f);                         // mục 5.5.5 — có thể return (đã sửa xong)
        return;
    case 2:
        panic_frame(f, "NMI");
    case 8:
        panic_frame(f, "double fault (kernel stack overflow?)");
    default:
        if (frame_from_user(f)) {                 // lỗi của chương trình user → giết process, không panic
            kprintf("process %u: exception %lu at %p\n", ps_current_pid(), f->vector, f->rip);
            ps_terminate_current(E_FAULT);        // không return
        }
        panic_frame(f, exception_name(f->vector));
    }
}
```

#### 5.2.5 PIC 8259

```cpp
// kernel/arch/x86_64/pic.cpp
namespace {
constexpr uint16_t PIC1_CMD = 0x20, PIC1_DATA = 0x21;
constexpr uint16_t PIC2_CMD = 0xA0, PIC2_DATA = 0xA1;
constexpr uint8_t  PIC_EOI  = 0x20;
constexpr uint8_t  OCW3_READ_ISR = 0x0B;
}

void pic_remap_and_mask_all() {
    outb(PIC1_CMD, 0x11); io_wait();    // ICW1: bắt đầu khởi tạo, sẽ có ICW4
    outb(PIC2_CMD, 0x11); io_wait();
    outb(PIC1_DATA, 32);  io_wait();    // ICW2: IRQ0-7  → vector 32-39 (ra khỏi vùng exception 0-31)
    outb(PIC2_DATA, 40);  io_wait();    //       IRQ8-15 → vector 40-47
    outb(PIC1_DATA, 0x04); io_wait();   // ICW3: master có slave ở chân IRQ2 (bitmask)
    outb(PIC2_DATA, 0x02); io_wait();   //       slave có ID = 2 (số)
    outb(PIC1_DATA, 0x01); io_wait();   // ICW4: chế độ 8086
    outb(PIC2_DATA, 0x01); io_wait();
    outb(PIC1_DATA, 0xFF);              // mask hết — driver unmask khi irq_register
    outb(PIC2_DATA, 0xFF);
}

void pic_unmask(uint8_t irq) {
    uint16_t port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    uint8_t bit = irq < 8 ? irq : irq - 8;
    outb(port, inb(port) & ~(1u << bit));
    if (irq >= 8) outb(PIC1_DATA, inb(PIC1_DATA) & ~(1u << 2));   // mở đường cascade
}

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

// IRQ7/IRQ15 có thể là "ma": PIC phát ngắt rồi rút lại. Đọc In-Service Register
// để phân biệt; ngắt ma thì KHÔNG gửi EOI cho chip đó.
bool pic_is_spurious(uint8_t irq) {
    uint16_t cmd = irq == 7 ? PIC1_CMD : PIC2_CMD;
    outb(cmd, OCW3_READ_ISR);
    return (inb(cmd) & 0x80) == 0;
}

// kernel/arch/x86_64/irq.cpp
using IrqHandler = void (*)(InterruptFrame*, void* ctx);
struct IrqSlot { IrqHandler fn; void* ctx; };
IrqSlot irq_table[16];

void irq_register(uint8_t irq, IrqHandler fn, void* ctx) {
    uint64_t flags = cpu_save_flags_and_cli();
    irq_table[irq] = {fn, ctx};
    pic_unmask(irq);
    cpu_restore_flags(flags);
}

void irq_dispatch(uint8_t irq, InterruptFrame* f) {
    if (irq == 7 && pic_is_spurious(7)) return;
    if (irq == 15 && pic_is_spurious(15)) { outb(0x20, 0x20); return; }   // master vẫn cần EOI (nó thấy IRQ2 thật)
    if (irq_table[irq].fn) irq_table[irq].fn(f, irq_table[irq].ctx);
    pic_send_eoi(irq);
}
```

#### 5.2.6 PIT

```cpp
// kernel/arch/x86_64/pit.cpp
void pit_init(uint32_t hz) {
    const uint32_t divisor = 1193182 / hz;        // 100Hz → 11931
    outb(0x43, 0x36);                             // kênh 0, lobyte/hibyte, mode 3 (square wave), nhị phân
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);
    irq_register(0, pit_isr, nullptr);
}

void pit_isr(InterruptFrame*, void*) {           // IRQL = DEVICE: chỉ đếm và đặt cờ
    Cpu& cpu = this_cpu();
    cpu.ticks++;
    timer_on_tick(cpu.ticks);                     // có timer hết hạn → ke_queue_dpc(timer_dpc)
    sched_on_tick();                              // hết quantum → cpu.need_resched = true
}
```

### 5.3 `lib/` — nền tảng dùng chung

#### 5.3.1 Runtime C++ tối thiểu

```cpp
// kernel/lib/cxx_runtime.cpp
#include <cstddef>

extern "C" void __cxa_pure_virtual() { panic("pure virtual call"); }

// Giữ mặc định `-fuse-cxa-atexit` của g++ và stub 2 symbol này.
// (Dùng -fno-use-cxa-atexit thì g++ chuyển sang gọi atexit() — lại phải stub atexit.)
void* __dso_handle = nullptr;
extern "C" int __cxa_atexit(void (*)(void*), void*, void*) { return 0; }   // kernel không bao giờ "exit"

using Constructor = void (*)();
extern "C" Constructor __init_array_start[];
extern "C" Constructor __init_array_end[];

void run_global_constructors() {
    for (Constructor* c = __init_array_start; c != __init_array_end; ++c) {
        (*c)();
    }
}

// kernel/mm/heap_cxx.cpp — nối operator new/delete vào kmalloc (mục 5.5.4)
void* operator new(size_t n)            { return kmalloc_or_panic(n, TAG('n','e','w',' ')); }
void* operator new[](size_t n)          { return kmalloc_or_panic(n, TAG('n','e','w','[')); }
void  operator delete(void* p) noexcept               { kfree(p); }
void  operator delete(void* p, size_t) noexcept       { kfree(p); }   // g++ sinh lời gọi này cho class có destructor ảo
void  operator delete[](void* p) noexcept             { kfree(p); }
void  operator delete[](void* p, size_t) noexcept     { kfree(p); }
inline void* operator new(size_t, void* where) noexcept { return where; }   // placement new, không include <new>
```

#### 5.3.2 `string.asm` — memcpy/memset/memmove/memcmp

g++ **vẫn** sinh lời gọi tới 4 hàm này dù `-ffreestanding` (khi copy struct,
khởi tạo mảng). Viết bằng asm thay vì C++ vì ở `-O2`, g++ có thể nhận ra vòng
lặp trong `memset` tự viết và thay nó bằng… lời gọi `memset` → đệ quy vô hạn.

```nasm
; kernel/lib/string.asm — System V: rdi, rsi, rdx; trả về rax
BITS 64
section .text

global memcpy
memcpy:                     ; void* memcpy(void* dst, const void* src, size_t n)
    mov rax, rdi
    mov rcx, rdx
    rep movsb               ; CPU hiện đại tối ưu sẵn (ERMSB), không cần tự tối ưu
    ret

global memset
memset:                     ; void* memset(void* dst, int c, size_t n)
    mov r9, rdi
    mov eax, esi
    mov rcx, rdx
    rep stosb
    mov rax, r9
    ret

global memmove
memmove:                    ; void* memmove(void* dst, const void* src, size_t n)
    mov rax, rdi
    mov rcx, rdx
    cmp rdi, rsi
    jbe .forward            ; dst <= src: copy xuôi luôn an toàn
    lea r8, [rsi + rdx]
    cmp rdi, r8
    jae .forward            ; dst nằm sau hẳn vùng src: không chồng lấn
    lea rsi, [rsi + rdx - 1]
    lea rdi, [rdi + rdx - 1]
    std                     ; chồng lấn và dst > src: copy ngược từ cuối
    rep movsb
    cld                     ; BẮT BUỘC trả DF về 0 (ABI)
    ret
.forward:
    rep movsb
    ret

global memcmp
memcmp:                     ; int memcmp(const void* a, const void* b, size_t n)
    xor eax, eax
    mov rcx, rdx
    test rcx, rcx
    jz .done
    repe cmpsb              ; so [rsi] với [rdi] tới khi khác nhau hoặc hết
    je .done                ; ZF=1: mọi byte bằng nhau
    movzx eax, byte [rdi - 1]
    movzx ecx, byte [rsi - 1]
    sub eax, ecx            ; a[i] - b[i]
.done:
    ret
```

#### 5.3.3 `kprintf`, `panic`, `KASSERT`, backtrace

```cpp
// kernel/lib/kprintf.cpp
#include <cstdarg>

namespace {
IrqSpinLock console_lock;          // kprintf được gọi cả từ ISR → khoá loại cli (4.8)
bool panicking = false;

void putc_all(char c) {
    if (c == '\n') serial_putc('\r');
    serial_putc(c);
    vga_console_putc(c);
}
void puts_all(const char* s) { while (*s) putc_all(*s++); }

void print_unsigned(uint64_t v, unsigned base, int width, char pad) {
    char buf[32];
    int n = 0;
    do {
        buf[n++] = "0123456789abcdef"[v % base];
        v /= base;
    } while (v != 0);
    while (n < width && n < 32) buf[n++] = pad;
    while (n > 0) putc_all(buf[--n]);
}
}  // namespace

void vkprintf(const char* fmt, va_list ap) {
    for (; *fmt; fmt++) {
        if (*fmt != '%') { putc_all(*fmt); continue; }
        fmt++;
        if (*fmt == '\0') break;                               // '%' ở cuối chuỗi

        char pad = ' ';
        int width = 0;
        bool is_long = false;
        if (*fmt == '0') { pad = '0'; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        while (*fmt == 'l') { is_long = true; fmt++; }

        switch (*fmt) {
        case 'd': {
            int64_t v = is_long ? va_arg(ap, int64_t) : va_arg(ap, int);
            uint64_t magnitude = v < 0 ? ~static_cast<uint64_t>(v) + 1 : static_cast<uint64_t>(v);  // đúng cả INT64_MIN
            if (v < 0) putc_all('-');
            print_unsigned(magnitude, 10, width, pad);
            break;
        }
        case 'u': print_unsigned(is_long ? va_arg(ap, uint64_t) : va_arg(ap, unsigned), 10, width, pad); break;
        case 'x': print_unsigned(is_long ? va_arg(ap, uint64_t) : va_arg(ap, unsigned), 16, width, pad); break;
        case 'p': puts_all("0x"); print_unsigned(reinterpret_cast<uint64_t>(va_arg(ap, void*)), 16, 16, '0'); break;
        case 's': { const char* s = va_arg(ap, const char*); puts_all(s ? s : "(null)"); break; }
        case 'c': putc_all(static_cast<char>(va_arg(ap, int))); break;
        case '%': putc_all('%'); break;
        default:  putc_all('%'); putc_all(*fmt); break;
        }
    }
}

void kprintf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    if (panicking) {                  // đang panic: bỏ qua khoá (có thể chính khoá này đang bị giữ)
        vkprintf(fmt, ap);
    } else {
        auto st = console_lock.acquire();
        vkprintf(fmt, ap);
        console_lock.release(st);
    }
    va_end(ap);
}

// kernel/lib/panic.cpp
void backtrace(uint64_t rbp) {
    // Cần -fno-omit-frame-pointer: mỗi frame = [rbp] → rbp cũ, [rbp+8] → return address.
    for (int depth = 0; depth < 32 && rbp >= KERNEL_VMA && (rbp & 7) == 0; depth++) {
        const uint64_t* frame = reinterpret_cast<const uint64_t*>(rbp);
        kprintf("  #%d %p\n", depth, reinterpret_cast<void*>(frame[1]));
        rbp = frame[0];
    }
    kprintf("  (giai ma: addr2line -f -C -e kernel/kernel.elf <dia chi>)\n");
}
```

> Giới hạn của `backtrace`: chỉ đi theo frame nằm trong kernel image
> (`rbp >= KERNEL_VMA`). Frame trên kernel stack (vùng `KSTACK_BASE`) nằm dưới
> ngưỡng này — khi có kernel thread, đổi điều kiện thành "rbp nằm trong stack
> của thread hiện tại" để không dereference rác.

```cpp
[[noreturn]] void panic(const char* fmt, ...) {
    cpu_disable_interrupts();
    panicking = true;
    kprintf("\n*** KERNEL PANIC: ");
    va_list ap; va_start(ap, fmt); vkprintf(fmt, ap); va_end(ap);
    kprintf("\n");
    uint64_t rbp; asm volatile("mov %%rbp, %0" : "=r"(rbp));
    backtrace(rbp);
    for (;;) asm volatile("hlt");
}

[[noreturn]] void panic_frame(const InterruptFrame* f, const char* fmt, ...);   // in thêm toàn bộ thanh ghi + CR2/CR3, backtrace(f->rbp)

#define KASSERT(cond, ...) \
    do { if (!(cond)) panic("KASSERT(" #cond ") that bai tai " __FILE__ ":%d", __LINE__); } while (0)
```

#### 5.3.4 Danh sách liên kết xâm nhập (≈ `LIST_ENTRY`)

Kernel tránh cấp phát cho node danh sách: node nằm **bên trong** object
(Thread có `sched_link`, Dpc có `link`…). Một object nằm trong nhiều danh sách
thì có nhiều node.

```cpp
// kernel/lib/list.hpp
struct ListNode { ListNode* prev; ListNode* next; };

inline void list_init(ListNode* head) { head->prev = head->next = head; }   // ≈ InitializeListHead
inline bool list_empty(const ListNode* head) { return head->next == head; }
inline void list_insert_after(ListNode* pos, ListNode* n) {
    n->prev = pos; n->next = pos->next; pos->next->prev = n; pos->next = n;
}
inline void list_push_back(ListNode* head, ListNode* n) { list_insert_after(head->prev, n); }
inline void list_remove(ListNode* n) {
    n->prev->next = n->next; n->next->prev = n->prev;
    n->prev = n->next = n;          // node tự trỏ về mình: remove 2 lần không phá danh sách khác
}
#define container_of(ptr, Type, member) \
    (reinterpret_cast<Type*>(reinterpret_cast<char*>(ptr) - offsetof(Type, member)))   // ≈ CONTAINING_RECORD
```

Không dùng default member initializer `prev = this`: object global sẽ cần
constructor động, và có thể bị dùng trước khi `run_global_constructors` chạy.
Gọi `list_init` tường minh — giống `InitializeListHead` bên NT.

#### 5.3.5 Kernel self-test

```cpp
// kernel/lib/ktest.hpp — mỗi test là 1 con trỏ hàm nằm trong section .ktests
struct KTest { const char* name; bool (*fn)(); };
#define KTEST(name_)                                                       \
    static bool ktest_##name_();                                           \
    [[gnu::used, gnu::section(".ktests")]]                                 \
    static const KTest ktest_entry_##name_ = {#name_, ktest_##name_};      \
    static bool ktest_##name_()

// Ví dụ (mm/heap_test.cpp)
KTEST(heap_alloc_free_reuses_memory) {
    void* a = kmalloc(100, TAG('t','e','s','t'));
    kfree(a);
    void* b = kmalloc(100, TAG('t','e','s','t'));
    kfree(b);
    return a == b;
}

// Chạy khi build với KTEST=1: in "[PASS]/[FAIL] name", rồi thoát QEMU qua isa-debug-exit
void ktest_run_all() {
    extern const KTest __ktests_start[], __ktests_end[];
    int failed = 0;
    for (const KTest* t = __ktests_start; t != __ktests_end; ++t) {
        const bool ok = t->fn();
        kprintf("[%s] %s\n", ok ? "PASS" : "FAIL", t->name);
        failed += ok ? 0 : 1;
    }
    kprintf(failed == 0 ? "ALL TESTS PASSED\n" : "%d TEST(S) FAILED\n", failed);
    outb(0xF4, failed == 0 ? 0x10 : 0x11);   // QEMU thoát với mã (v << 1) | 1 → 33 hoặc 35
}
```

### 5.4 `ke/` — lõi kernel: IRQL, DPC, timer, scheduler, đồng bộ hoá

#### 5.4.1 IRQL

Khác NT một điểm có chủ ý: `ke_raise_irql` nâng lên `max(hiện tại, yêu cầu)`
thay vì assert. Nhờ đó `SpinLock` dùng được cả khi đang boot ở `HIGH`.

```cpp
// kernel/ke/irql.cpp
Irql ke_get_irql() { return this_cpu().irql; }

Irql ke_raise_irql(Irql requested) {
    Cpu& cpu = this_cpu();
    const Irql old = cpu.irql;
    if (requested > old) {
        if (requested >= Irql::Device) cpu_disable_interrupts();
        cpu.irql = requested;
    }
    return old;
}

void ke_lower_irql(Irql target) {
    Cpu& cpu = this_cpu();
    KASSERT(target <= cpu.irql, "ke_lower_irql: target cao hon IRQL hien tai");

    if (target >= Irql::Dispatch) {
        cpu.irql = target;
        if (target < Irql::Device) cpu_enable_interrupts();
        return;
    }

    // Hạ về PASSIVE: xử lý hết việc bị hoãn TRƯỚC. Kiểm tra cờ khi IF=0 để
    // không có ngắt nào chen vào giữa "kiểm tra xong" và "đã hạ IRQL".
    cpu.irql = Irql::Dispatch;
    for (;;) {
        cpu_disable_interrupts();
        if (cpu.dpc_pending) {
            cpu_enable_interrupts();
            dpc_drain();
            continue;
        }
        if (cpu.need_resched) {
            sched_schedule_locked();          // IRQL=DISPATCH, IF=0 ✓ bất biến 4.8
            continue;
        }
        break;                                // IF = 0, không còn việc hoãn
    }
    cpu.irql = target;
    cpu_enable_interrupts();                  // ngắt đang chờ (nếu có) tới ngay đây, thấy old=PASSIVE → tự xử lý
}
```

#### 5.4.2 Khoá

```cpp
// kernel/ke/spinlock.hpp — bản 1 CPU. Khi làm SMP: `locked_` thành cờ atomic + vòng `pause`.
class SpinLock {
public:
    [[nodiscard]] Irql acquire() {
        const Irql old = ke_raise_irql(Irql::Dispatch);
        KASSERT(!locked_, "SpinLock da bi giu — tren 1 CPU day chac chan la deadlock/de quy");
        locked_ = true;
        return old;
    }
    void release(Irql old) {
        KASSERT(locked_, "release SpinLock chua acquire");
        locked_ = false;
        ke_lower_irql(old);
    }
private:
    bool locked_ = false;
};

// Dữ liệu dùng chung với ISR. Không đụng this_cpu() → dùng được từ dòng đầu tiên của kernel_main.
class IrqSpinLock {
public:
    [[nodiscard]] uint64_t acquire() {
        const uint64_t flags = cpu_save_flags_and_cli();
        locked_ = true;
        return flags;
    }
    void release(uint64_t flags) {
        locked_ = false;
        cpu_restore_flags(flags);
    }
private:
    bool locked_ = false;
};

// RAII cho trường hợp đơn giản
class SpinLockGuard {
public:
    explicit SpinLockGuard(SpinLock& l) : lock_(l), old_(l.acquire()) {}
    ~SpinLockGuard() { lock_.release(old_); }
    SpinLockGuard(const SpinLockGuard&) = delete;
    SpinLockGuard& operator=(const SpinLockGuard&) = delete;
private:
    SpinLock& lock_;
    Irql old_;
};
```

#### 5.4.3 DPC (Deferred Procedure Call)

**Vì sao cần:** ISR chạy với ngắt tắt — càng dài thì các thiết bị khác càng
phải chờ. ISR chỉ làm phần bắt buộc (đọc thanh ghi thiết bị, xác nhận ngắt),
phần còn lại (dịch scancode, đánh thức thread) dồn sang DPC chạy ở
`DISPATCH` với ngắt bật.

```cpp
// kernel/ke/dpc.hpp
struct Dpc {
    ListNode link;
    void (*routine)(Dpc* dpc, void* ctx);
    void* ctx;
    bool queued;
};
inline void dpc_setup(Dpc& d, void (*routine)(Dpc*, void*), void* ctx) {   // ≈ KeInitializeDpc
    list_init(&d.link); d.routine = routine; d.ctx = ctx; d.queued = false;
}

// kernel/ke/dpc.cpp
namespace { ListNode dpc_queue; IrqSpinLock dpc_lock; }

void dpc_init() { list_init(&dpc_queue); }

void ke_queue_dpc(Dpc& d) {                    // ≈ KeInsertQueueDpc — gọi được ở MỌI IRQL
    const uint64_t flags = dpc_lock.acquire();
    if (!d.queued) {                           // xếp 2 lần trước khi chạy = chạy 1 lần (giống NT)
        d.queued = true;
        list_push_back(&dpc_queue, &d.link);
        this_cpu().dpc_pending = true;
    }
    dpc_lock.release(flags);
}

void dpc_drain() {                             // gọi ở IRQL == DISPATCH
    for (;;) {
        const uint64_t flags = dpc_lock.acquire();
        if (list_empty(&dpc_queue)) {
            this_cpu().dpc_pending = false;
            dpc_lock.release(flags);
            return;
        }
        Dpc* d = container_of(dpc_queue.next, Dpc, link);
        list_remove(&d->link);
        d->queued = false;
        dpc_lock.release(flags);
        d->routine(d, d->ctx);                 // chạy ngoài khoá → routine được phép tự xếp lại chính nó
    }
}
```

#### 5.4.4 Timer & sleep

ISR timer không được duyệt danh sách (danh sách có thể đang bị sửa dở ở
`DISPATCH`). Nó chỉ so một số nguyên `next_due_tick`; ghi một `uint64_t` là
một lệnh `mov` duy nhất nên không bị đọc dở.

```cpp
// kernel/ke/timer.cpp
namespace {
ListNode sleepers;                               // sắp xếp tăng dần theo wake_tick, chỉ sửa ở IRQL DISPATCH
uint64_t next_due_tick = UINT64_MAX;             // ISR chỉ đọc
Dpc timer_dpc;
constexpr uint64_t HZ = 100;

void timer_dpc_routine(Dpc*, void*) {            // IRQL DISPATCH
    const uint64_t now = this_cpu().ticks;
    while (!list_empty(&sleepers)) {
        Thread* t = container_of(sleepers.next, Thread, timer_link);
        if (t->wake_tick > now) break;
        list_remove(&t->timer_link);
        sched_ready(t);
    }
    next_due_tick = list_empty(&sleepers) ? UINT64_MAX
                                          : container_of(sleepers.next, Thread, timer_link)->wake_tick;
}
}  // namespace

void timer_init() { list_init(&sleepers); dpc_setup(timer_dpc, timer_dpc_routine, nullptr); }

void timer_on_tick(uint64_t now) {               // từ pit_isr, IRQL DEVICE
    if (now >= next_due_tick) ke_queue_dpc(timer_dpc);
}

void ke_sleep_ms(uint64_t ms) {
    KASSERT(ke_get_irql() == Irql::Passive, "sleep chi o PASSIVE");
    Thread* self = this_cpu().current;
    const Irql old = ke_raise_irql(Irql::Dispatch);

    self->wake_tick = this_cpu().ticks + (ms * HZ + 999) / 1000;   // làm tròn lên, ngủ ít nhất `ms`
    ListNode* pos = &sleepers;
    while (pos->next != &sleepers && container_of(pos->next, Thread, timer_link)->wake_tick <= self->wake_tick)
        pos = pos->next;
    list_insert_after(pos, &self->timer_link);
    if (self->wake_tick < next_due_tick) next_due_tick = self->wake_tick;

    self->state = ThreadState::Waiting;
    cpu_disable_interrupts();
    sched_schedule_locked();                     // ngủ ở đây; timer_dpc gọi sched_ready → quay lại
    ke_lower_irql(old);
}
```

#### 5.4.5 Thread, scheduler, context switch

```cpp
// kernel/ke/thread.hpp
enum class ThreadState : uint8_t { Ready, Running, Waiting, Terminated };

struct Thread {
    ObjectHeader header;              // ob/ — thread cũng là object có handle được (mục 5.6)
    uint64_t     kernel_rsp;          // RSP đã cất khi thread KHÔNG chạy (switch_context ghi/đọc)
    VirtAddr     kernel_stack_top;
    ThreadState  state;
    uint32_t     tid;
    uint32_t     quantum_left;
    Process*     process;             // nullptr = kernel thread
    ListNode     sched_link;          // ready queue HOẶC wait list — không bao giờ cả hai
    ListNode     timer_link;
    ListNode     process_link;
    uint64_t     wake_tick;
    const char*  name;
};
```

**Context switch — trái tim của đa nhiệm.** Chỉ cần lưu callee-saved register
(rbx, rbp, r12–r15) + RSP: mọi register khác đã được *người gọi* hàm này
coi là bị phá (theo ABI), hoặc đã nằm trong `InterruptFrame` trên stack.

```nasm
; kernel/arch/x86_64/context_switch.asm
; void switch_context(uint64_t* save_rsp_slot, uint64_t new_rsp)
;   rdi = &prev->kernel_rsp, rsi = next->kernel_rsp
global switch_context
switch_context:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov [rdi], rsp          ; ── thread cũ "đông cứng" với 6 register trên stack của nó
    mov rsp, rsi            ; ── từ lệnh này trở đi đang ở stack của thread mới
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret                     ; return vào nơi thread mới từng gọi switch_context
                            ; (hoặc vào thread_trampoline nếu thread mới tinh)

; Thread mới lần đầu chạy: switch_context `ret` vào đây với r12=entry, r13=arg
extern thread_start
global thread_trampoline
thread_trampoline:
    mov rdi, r12
    mov rsi, r13
    call thread_start       ; [[noreturn]]
    ud2
```

```cpp
// kernel/ke/thread.cpp
constexpr size_t KSTACK_PAGES = 4;                   // 16KB + 1 guard page

// process == nullptr → kernel thread. Thread user (5.7.3) cũng tạo bằng hàm này:
// nó bắt đầu như kernel thread rồi tự `iretq` xuống ring 3 trong entry.
Thread* ke_create_thread(Process* process, void (*entry)(void*), void* arg, const char* name) {
    Thread* t = new Thread{};
    t->process = process;
    t->kernel_stack_top = kstack_alloc(KSTACK_PAGES); // mục 5.5.4: guard page không map ở đáy

    // Dựng stack giả sao cho switch_context "pop" ra đúng giá trị ban đầu:
    // thứ tự pop là r15, r14, r13, r12, rbx, rbp, ret → dựng ngược từ đỉnh xuống.
    auto* sp = reinterpret_cast<uint64_t*>(t->kernel_stack_top);
    *--sp = reinterpret_cast<uint64_t>(thread_trampoline);   // ret
    *--sp = 0;                                               // rbp = 0 → backtrace dừng ở đây
    *--sp = 0;                                               // rbx
    *--sp = reinterpret_cast<uint64_t>(entry);               // r12
    *--sp = reinterpret_cast<uint64_t>(arg);                 // r13
    *--sp = 0;                                               // r14
    *--sp = 0;                                               // r15
    t->kernel_rsp = reinterpret_cast<uint64_t>(sp);
    // Căn chỉnh: sau 7 lần pop, RSP = stack_top (bội 16) → `call thread_start`
    // đẩy return address → thread_start vào với RSP%16 == 8 đúng ABI.

    t->tid = next_tid++;
    t->name = name;
    const Irql old = ke_raise_irql(Irql::Dispatch);
    sched_ready(t);
    ke_lower_irql(old);
    return t;
}

extern "C" [[noreturn]] void thread_start(void (*entry)(void*), void* arg) {
    // Tới đây từ sched_schedule_locked: IRQL=DISPATCH, IF=0 (bất biến 4.8)
    this_cpu().irql = Irql::Passive;
    cpu_enable_interrupts();
    entry(arg);
    ke_exit_thread();
}
```

```cpp
// kernel/ke/sched.cpp
namespace {
ListNode ready_queue;
ListNode zombies;                 // thread đã chết, chờ reaper giải phóng stack
Thread*  idle_thread;
KEvent   reaper_event;
constexpr uint32_t QUANTUM_TICKS = 5;   // 50ms ở 100Hz
}

void sched_init() {
    list_init(&ready_queue);
    list_init(&zombies);
    ke_event_init(reaper_event, EventType::Synchronization, false);
    // Luồng boot đang chạy trở thành thread 0 — không cần stack mới, nó đã có stack trong .bss
    idle_thread = new Thread{};
    idle_thread->state = ThreadState::Running;
    idle_thread->name = "idle";
    this_cpu().current = idle_thread;
    // Reaper: thread PASSIVE giải phóng stack + Thread của thread đã chết (chờ reaper_event).
    // Tạo ngay ở đây nhưng chỉ thực sự chạy sau ke_lower_irql(PASSIVE) trong kernel_main.
    ke_create_thread(nullptr, reaper_main, nullptr, "reaper");
}

void sched_ready(Thread* t) {                  // IRQL >= DISPATCH
    t->state = ThreadState::Ready;
    list_push_back(&ready_queue, &t->sched_link);
    if (this_cpu().current == idle_thread) this_cpu().need_resched = true;
}

void sched_on_tick() {                         // IRQL DEVICE — chỉ đọc/ghi thread hiện tại
    Cpu& cpu = this_cpu();
    Thread* t = cpu.current;
    if (t != idle_thread && t->quantum_left > 0 && --t->quantum_left == 0) cpu.need_resched = true;
}

void sched_schedule_locked() {                 // BẤT BIẾN: IRQL == DISPATCH, IF == 0
    Cpu& cpu = this_cpu();
    cpu.need_resched = false;
    Thread* prev = cpu.current;

    Thread* next = nullptr;
    if (!list_empty(&ready_queue)) {
        next = container_of(ready_queue.next, Thread, sched_link);
        list_remove(&next->sched_link);
    }

    if (prev->state == ThreadState::Running) {
        if (next == nullptr) {                 // không ai chờ → prev chạy tiếp với quantum mới
            prev->quantum_left = QUANTUM_TICKS;
            return;
        }
        if (prev != idle_thread) {             // idle không bao giờ nằm trong ready queue
            prev->state = ThreadState::Ready;
            list_push_back(&ready_queue, &prev->sched_link);
        }
    }
    if (next == nullptr) next = idle_thread;   // prev đang Waiting/Terminated và không ai Ready
    if (next == prev) return;

    next->state = ThreadState::Running;
    next->quantum_left = QUANTUM_TICKS;
    cpu.current = next;
    cpu.kernel_rsp = next->kernel_stack_top;   // syscall_entry lấy stack ở đây
    tss_set_rsp0(next->kernel_stack_top);      // ngắt từ ring 3 lấy stack ở đây

    // Kernel thread dùng address space của kernel: nếu nó "mượn" CR3 của một
    // process vừa chết, page table đó có thể đã bị giải phóng dưới chân nó.
    const PhysAddr next_cr3 = next->process ? next->process->address_space->pml4 : kernel_as.pml4;
    if (read_cr3() != next_cr3) write_cr3(next_cr3);

    switch_context(&prev->kernel_rsp, next->kernel_rsp);
    // Tới được dòng này nghĩa là `prev` (chính thread đang chạy code này) đã được chọn lại.
}

[[noreturn]] void ke_exit_thread() {
    const Irql old = ke_raise_irql(Irql::Dispatch);
    (void)old;
    Thread* self = this_cpu().current;
    self->state = ThreadState::Terminated;
    list_push_back(&zombies, &self->sched_link);
    ke_event_set(reaper_event);               // reaper (thread PASSIVE) giải phóng stack — không thể tự free stack đang đứng trên
    cpu_disable_interrupts();
    sched_schedule_locked();
    panic("thread da chet duoc schedule lai");
}

[[noreturn]] void sched_become_idle() {
    this_cpu().current->name = "idle";
    for (;;) {
        cpu_enable_interrupts();
        asm volatile("hlt");                      // `sti; hlt` liền nhau: CPU trì hoãn ngắt 1 lệnh sau sti → không lỡ ngắt
    }
}
```

#### 5.4.6 Event & Mutex (≈ dispatcher object)

```cpp
// kernel/ke/wait.hpp
enum class EventType : uint8_t {
    Notification,        // set → đánh thức TẤT CẢ, giữ trạng thái signaled tới khi reset (≈ NotificationEvent)
    Synchronization,     // set → đánh thức 1, tự reset khi thread đó nhận  (≈ SynchronizationEvent)
};
struct KEvent { EventType type; bool signaled; ListNode waiters; };
struct KMutex { Thread* owner; uint32_t recursion; ListNode waiters; };

// kernel/ke/wait.cpp
void ke_event_init(KEvent& e, EventType type, bool signaled) {
    e.type = type; e.signaled = signaled; list_init(&e.waiters);
}

void ke_event_wait(KEvent& e) {
    KASSERT(ke_get_irql() == Irql::Passive, "chi duoc cho o PASSIVE");
    Thread* self = this_cpu().current;
    const Irql old = ke_raise_irql(Irql::Dispatch);
    // Trên 1 CPU, IRQL DISPATCH đã chặn DPC và preempt → không ai set event
    // chen vào giữa "kiểm tra signaled" và "xếp vào waiters" (lost wakeup).
    while (!e.signaled) {
        self->state = ThreadState::Waiting;
        list_push_back(&e.waiters, &self->sched_link);
        const uint64_t flags = cpu_save_flags_and_cli();
        sched_schedule_locked();
        cpu_restore_flags(flags);
    }
    if (e.type == EventType::Synchronization) e.signaled = false;
    ke_lower_irql(old);
}

void ke_event_set(KEvent& e) {                  // IRQL <= DISPATCH (thread hoặc DPC), KHÔNG từ ISR
    const Irql old = ke_raise_irql(Irql::Dispatch);
    e.signaled = true;
    while (!list_empty(&e.waiters)) {
        Thread* t = container_of(e.waiters.next, Thread, sched_link);
        list_remove(&t->sched_link);
        sched_ready(t);
        if (e.type == EventType::Synchronization) break;
    }
    ke_lower_irql(old);                         // nếu vừa ở PASSIVE → có thể preempt ngay tại đây
}

void ke_event_reset(KEvent& e) {                // ≈ KeResetEvent
    const Irql old = ke_raise_irql(Irql::Dispatch);
    e.signaled = false;
    ke_lower_irql(old);
}

void ke_mutex_acquire(KMutex& m) {
    KASSERT(ke_get_irql() == Irql::Passive, "KMutex chi dung o PASSIVE");
    Thread* self = this_cpu().current;
    const Irql old = ke_raise_irql(Irql::Dispatch);
    while (m.owner != nullptr && m.owner != self) {
        self->state = ThreadState::Waiting;
        list_push_back(&m.waiters, &self->sched_link);
        const uint64_t flags = cpu_save_flags_and_cli();
        sched_schedule_locked();
        cpu_restore_flags(flags);               // thức dậy → vòng lặp kiểm tra lại (thread khác có thể giành trước)
    }
    m.owner = self;
    m.recursion++;
    ke_lower_irql(old);
}

void ke_mutex_release(KMutex& m) {
    const Irql old = ke_raise_irql(Irql::Dispatch);
    KASSERT(m.owner == this_cpu().current, "release mutex khong so huu");
    if (--m.recursion == 0) {
        m.owner = nullptr;
        if (!list_empty(&m.waiters)) {
            Thread* t = container_of(m.waiters.next, Thread, sched_link);
            list_remove(&t->sched_link);
            sched_ready(t);
        }
    }
    ke_lower_irql(old);
}
```

> **Chưa xử lý (ghi nhận để học sau):** priority inversion (NT dùng priority
> boost cho owner), wait có timeout, wait nhiều object — đều mở rộng được
> trên cấu trúc này.

### 5.5 `mm/` — quản lý bộ nhớ

Chuỗi phụ thuộc bên trong `mm/`:

```
E820 → PMM (frame vật lý) → paging (PTE, walk) → AddressSpace/VMA → heap (kmalloc) → page fault, user copy
```

#### 5.5.1 PMM — giữ bitmap, mở rộng API

```cpp
// kernel/mm/pmm.hpp
void     pmm_init(const BootInfo& boot);
PhysAddr pmm_alloc_frame();                         // 0 = hết bộ nhớ
PhysAddr pmm_alloc_frame_below(PhysAddr limit);     // cho page table lúc HHDM mới phủ 1GB (5.5.3)
void     pmm_free_frame(PhysAddr frame);
uint64_t pmm_free_frames();
PhysAddr pmm_highest_usable_address();              // VMM dùng để biết phải direct-map tới đâu
```

Thay đổi so với `pmm.cpp` hiện tại:

1. `pmm_init(const BootInfo&)`: đọc E820 qua `phys_to_virt(boot.e820_phys)`;
   khoá `[kernel_phys_start, kernel_phys_end)` và `[initrd_phys, +initrd_size)`
   thay cho `__kernel_end` (giờ là địa chỉ ảo).
2. Mọi hàm `alloc`/`free` lấy `SpinLock pmm_lock`.
3. `pmm_alloc_frame_below(limit)`: quét từ frame 0 tới `limit / FRAME_SIZE`.
4. Bỏ toàn bộ code in VGA trong `pmm.cpp` → `kprintf`.

**Nâng cấp sau (khi làm shared memory/copy-on-write):** thay bitmap bằng mảng
`struct PageFrame { uint32_t refcount; uint16_t flags; ListNode link; }` —
một phần tử cho mỗi frame, nằm trong HHDM. Đây đúng là **PFN database** của
Windows; bitmap không đủ vì COW cần biết một frame đang được bao nhiêu PTE
tham chiếu.

#### 5.5.2 Paging primitives

```cpp
// kernel/arch/x86_64/paging.hpp
enum : uint64_t {
    PTE_PRESENT  = 1ULL << 0,
    PTE_WRITABLE = 1ULL << 1,
    PTE_USER     = 1ULL << 2,
    PTE_ACCESSED = 1ULL << 5,
    PTE_DIRTY    = 1ULL << 6,
    PTE_HUGE     = 1ULL << 7,     // ở cấp PD: trang 2MB
    PTE_GLOBAL   = 1ULL << 8,
    PTE_NX       = 1ULL << 63,    // chỉ hợp lệ khi EFER.NXE = 1, nếu không → #PF reserved bit
};
constexpr uint64_t PTE_ADDR_MASK = 0x000FFFFFFFFFF000ULL;

// level: 4 = PML4, 3 = PDPT, 2 = PD, 1 = PT
inline size_t pt_index(VirtAddr va, int level) { return (va >> (12 + 9 * (level - 1))) & 0x1FF; }

// Page fault error code (Intel SDM §4.7)
enum : uint64_t { PF_PRESENT = 1, PF_WRITE = 2, PF_USER = 4, PF_RESERVED = 8, PF_INSTRUCTION = 16 };
```

```cpp
// kernel/mm/vmm.cpp
namespace {
PhysAddr hhdm_mapped_limit = 1ULL << 30;   // Stage2 mới direct-map 1GB; vmm_init nâng lên toàn RAM

// Trả con trỏ tới entry ở cấp `target_level` quản lý `va`.
uint64_t* pt_walk(PhysAddr pml4, VirtAddr va, int target_level, bool create) {
    const bool user = va < USER_TOP;
    auto* table = reinterpret_cast<uint64_t*>(phys_to_virt(pml4));
    for (int level = 4; level > target_level; level--) {
        uint64_t& entry = table[pt_index(va, level)];
        if (!(entry & PTE_PRESENT)) {
            if (!create) return nullptr;
            // Bảng mới phải nằm trong vùng HHDM đã map, nếu không không ghi được vào nó
            const PhysAddr frame = pmm_alloc_frame_below(hhdm_mapped_limit);
            if (frame == 0) return nullptr;
            memset(reinterpret_cast<void*>(phys_to_virt(frame)), 0, PAGE_SIZE);
            // Entry trung gian cấp quyền rộng nhất; quyền thật do entry lá quyết định
            // (CPU lấy AND của mọi cấp → cấp trung gian thiếu W/U là chặn luôn).
            entry = frame | PTE_PRESENT | PTE_WRITABLE | (user ? PTE_USER : 0);
        }
        if (entry & PTE_HUGE) return nullptr;
        table = reinterpret_cast<uint64_t*>(phys_to_virt(entry & PTE_ADDR_MASK));
    }
    return &table[pt_index(va, target_level)];
}
}  // namespace

Status vmm_map_page(AddressSpace& as, VirtAddr va, PhysAddr pa, uint64_t flags) {
    KASSERT(((va | pa) & 0xFFF) == 0, "map: dia chi chua can trang");
    KASSERT((va < USER_TOP) == ((flags & PTE_USER) != 0), "map: co User phai khop nua dia chi");
    SpinLockGuard guard(as.lock);                  // thứ tự khoá: AddressSpace.lock → pmm_lock
    uint64_t* pte = pt_walk(as.pml4, va, 1, true);
    if (pte == nullptr) return E_NOMEM;
    if (*pte & PTE_PRESENT) return E_BUSY;         // không bao giờ âm thầm ghi đè mapping cũ
    *pte = pa | flags | PTE_PRESENT;
    return OK;                                     // map mới không cần invlpg: TLB không cache entry not-present
}

PhysAddr vmm_unmap_page(AddressSpace& as, VirtAddr va) {   // trả frame, caller quyết định free hay không
    SpinLockGuard guard(as.lock);
    uint64_t* pte = pt_walk(as.pml4, va, 1, false);
    if (pte == nullptr || !(*pte & PTE_PRESENT)) return 0;
    const PhysAddr pa = *pte & PTE_ADDR_MASK;
    *pte = 0;
    // Nửa kernel dùng chung mọi CR3 → luôn phải xoá TLB. Nửa user chỉ khi đang là CR3 hiện tại.
    if (va >= HHDM_BASE || read_cr3() == as.pml4) invlpg(va);
    return pa;
}
```

#### 5.5.3 `vmm_init` và address space

```cpp
// kernel/mm/address_space.hpp
enum VmFlags : uint32_t { VM_READ = 1, VM_WRITE = 2, VM_EXEC = 4 };
struct VmArea { ListNode link; VirtAddr start; VirtAddr end; uint32_t flags; };   // [start, end)
struct AddressSpace { PhysAddr pml4; SpinLock lock; ListNode vmas; };            // vmas ≈ VAD tree (ở đây là list)
extern AddressSpace kernel_as;

inline uint64_t vma_to_pte_flags(const VmArea& v) {
    uint64_t f = PTE_USER;
    if (v.flags & VM_WRITE) f |= PTE_WRITABLE;
    if (!(v.flags & VM_EXEC)) f |= PTE_NX;
    return f;
}
```

```cpp
// kernel/mm/vmm.cpp
extern "C" char __text_start[], __text_end[], __rodata_start[], __rodata_end[],
                __data_start[], __kernel_end[];

namespace {
void map_kernel_range(char* start, char* end, uint64_t flags) {
    const VirtAddr s = align_down(reinterpret_cast<VirtAddr>(start), PAGE_SIZE);
    const VirtAddr e = align_up(reinterpret_cast<VirtAddr>(end), PAGE_SIZE);
    for (VirtAddr va = s; va < e; va += PAGE_SIZE) {
        const Status st = vmm_map_page(kernel_as, va, va - KERNEL_VMA, flags);
        KASSERT(st == OK, "map kernel image");
    }
}
}  // namespace

void vmm_init(const BootInfo&) {
    KASSERT(cpu_has_nx(), "CPU khong ho tro NX (cpuid 0x80000001 EDX bit 20)");
    wrmsr(MSR_EFER, rdmsr(MSR_EFER) | EFER_NXE);   // PHẢI bật trước khi có PTE nào mang bit 63

    kernel_as.pml4 = pmm_alloc_frame_below(hhdm_mapped_limit);
    KASSERT(kernel_as.pml4 != 0, "het frame cho PML4");
    memset(reinterpret_cast<void*>(phys_to_virt(kernel_as.pml4)), 0, PAGE_SIZE);
    list_init(&kernel_as.vmas);

    // 1) Cấp sẵn PDPT cho TOÀN BỘ nửa kernel (256 frame ≈ 1MB) — xem lý do ở 4.5
    auto* pml4 = reinterpret_cast<uint64_t*>(phys_to_virt(kernel_as.pml4));
    for (int i = 256; i < 512; i++) {
        const PhysAddr pdpt = pmm_alloc_frame_below(hhdm_mapped_limit);
        KASSERT(pdpt != 0, "het frame cho PDPT kernel");
        memset(reinterpret_cast<void*>(phys_to_virt(pdpt)), 0, PAGE_SIZE);
        pml4[i] = pdpt | PTE_PRESENT | PTE_WRITABLE;
    }

    // 2) HHDM toàn bộ RAM bằng trang 2MB (không dùng 1GB: CPU mặc định `qemu64` không có pdpe1gb)
    const PhysAddr top = align_up(pmm_highest_usable_address(), 2ULL << 20);
    for (PhysAddr p = 0; p < top; p += 2ULL << 20) {
        uint64_t* pde = pt_walk(kernel_as.pml4, HHDM_BASE + p, 2, true);
        KASSERT(pde != nullptr, "het frame cho PD cua HHDM");
        *pde = p | PTE_PRESENT | PTE_WRITABLE | PTE_HUGE | PTE_NX;
    }

    // 3) Kernel image với W^X theo từng section (trang 4KB)
    map_kernel_range(__text_start,   __text_end,   0);                          // R-X
    map_kernel_range(__rodata_start, __rodata_end, PTE_NX);                     // R--
    map_kernel_range(__data_start,   __kernel_end, PTE_WRITABLE | PTE_NX);      // RW-

    // 4) Đổi sang page table riêng: identity map biến mất tại đây.
    //    Mọi truy cập vật lý sau dòng này PHẢI qua phys_to_virt() (kể cả VGA 0xB8000!)
    write_cr3(kernel_as.pml4);
    hhdm_mapped_limit = top;
    write_cr0(read_cr0() | (1ULL << 16));          // CR0.WP: ring 0 cũng tôn trọng trang read-only
}

AddressSpace* vmm_create_address_space() {
    auto* as = new AddressSpace{};
    as->pml4 = pmm_alloc_frame();
    if (as->pml4 == 0) { delete as; return nullptr; }
    auto* dst = reinterpret_cast<uint64_t*>(phys_to_virt(as->pml4));
    auto* src = reinterpret_cast<uint64_t*>(phys_to_virt(kernel_as.pml4));
    for (int i = 0; i < 256; i++)   dst[i] = 0;         // nửa user: trống
    for (int i = 256; i < 512; i++) dst[i] = src[i];    // nửa kernel: trỏ CÙNG PDPT → dùng chung vĩnh viễn
    list_init(&as->vmas);
    return as;
}

Status vma_add(AddressSpace& as, VirtAddr start, VirtAddr end, uint32_t flags);   // từ chối nếu chồng lấn
const VmArea* vma_find(AddressSpace& as, VirtAddr addr);                          // gọi khi đang giữ as.lock
void vmm_destroy_address_space(AddressSpace* as);   // free frame lá + bảng của PML4[0..255], rồi PML4
```

#### 5.5.4 Kernel heap (`kmalloc`/`kfree`) + kernel stack

Thiết kế phiên bản 1: **free list sắp theo địa chỉ, first-fit, gộp khối kề
nhau khi free**. Đủ đơn giản để đọc hiểu trong một lần, đủ đúng để làm nền.
Nâng cấp phiên bản 2 (slab cho kích thước nhỏ cố định) khi đo thấy chậm.

```
Vùng ảo KHEAP_BASE ────────────────────────────────────────────► heap_end (tăng dần)
 ┌──────────┬───────────────┬──────────┬───────────┬───────────────────────┐
 │Header 16B│ dữ liệu user  │FreeBlock │  (trống)  │Header│ dữ liệu        │
 │size,magic│               │size,next─┼──────────►│      │                │
 └──────────┴───────────────┴──────────┴───────────┴───────────────────────┘
 Header.size và FreeBlock.size cùng offset 0 → một khối đổi vai trò mà không cần di chuyển dữ liệu
```

```cpp
// kernel/mm/heap.cpp
constexpr uint32_t TAG(char a, char b, char c, char d) {     // pool tag kiểu NT, đọc được trong memory dump
    return uint32_t(uint8_t(a)) | uint32_t(uint8_t(b)) << 8 | uint32_t(uint8_t(c)) << 16 | uint32_t(uint8_t(d)) << 24;
}

namespace {
constexpr size_t   HEAP_ALIGN = 16;
constexpr uint32_t MAGIC_USED = 0xA110CA7E;

struct Header    { size_t size; uint32_t magic; uint32_t tag; };   // 16B, đứng ngay trước con trỏ trả cho caller
struct FreeBlock { size_t size; FreeBlock* next; };                // 16B
static_assert(sizeof(Header) == 16 && sizeof(FreeBlock) == 16);

FreeBlock* free_list = nullptr;      // tăng dần theo địa chỉ → gộp khối chỉ cần nhìn 2 hàng xóm
VirtAddr   heap_end  = KHEAP_BASE;
SpinLock   heap_lock;

void insert_and_coalesce(FreeBlock* b) {
    FreeBlock* prev = nullptr;
    FreeBlock* cur = free_list;
    while (cur != nullptr && cur < b) { prev = cur; cur = cur->next; }

    b->next = cur;
    if (prev) prev->next = b; else free_list = b;

    if (cur && reinterpret_cast<char*>(b) + b->size == reinterpret_cast<char*>(cur)) {        // gộp với khối sau
        b->size += cur->size;
        b->next = cur->next;
    }
    if (prev && reinterpret_cast<char*>(prev) + prev->size == reinterpret_cast<char*>(b)) {   // gộp với khối trước
        prev->size += b->size;
        prev->next = b->next;
    }
}

bool grow(size_t min_bytes) {                                  // gọi khi đang giữ heap_lock
    const size_t pages = align_up(min_bytes, PAGE_SIZE) / PAGE_SIZE;
    if (heap_end + pages * PAGE_SIZE > KHEAP_LIMIT) return false;
    size_t mapped = 0;
    for (; mapped < pages; mapped++) {
        const PhysAddr f = pmm_alloc_frame();
        if (f == 0) break;
        if (vmm_map_page(kernel_as, heap_end + mapped * PAGE_SIZE, f, PTE_WRITABLE | PTE_NX) != OK) {
            pmm_free_frame(f);
            break;
        }
    }
    if (mapped == 0) return false;
    auto* b = reinterpret_cast<FreeBlock*>(heap_end);
    b->size = mapped * PAGE_SIZE;
    heap_end += mapped * PAGE_SIZE;
    insert_and_coalesce(b);                                    // có thể gộp với khối trống cuối heap
    return mapped == pages;
}
}  // namespace

void* kmalloc(size_t n, uint32_t tag) {
    if (n == 0 || n > (1ULL << 32)) return nullptr;
    const size_t need = align_up(n + sizeof(Header), HEAP_ALIGN);
    SpinLockGuard guard(heap_lock);                            // IRQL DISPATCH → cấm gọi từ ISR (4.8)
    for (;;) {
        FreeBlock* prev = nullptr;
        for (FreeBlock* b = free_list; b != nullptr; prev = b, b = b->next) {
            if (b->size < need) continue;
            FreeBlock* after = b->next;
            if (b->size - need >= sizeof(FreeBlock) + HEAP_ALIGN) {   // phần dư đủ lớn → tách
                auto* rest = reinterpret_cast<FreeBlock*>(reinterpret_cast<char*>(b) + need);
                rest->size = b->size - need;
                rest->next = after;
                after = rest;
                b->size = need;
            }
            if (prev) prev->next = after; else free_list = after;
            auto* h = reinterpret_cast<Header*>(b);            // h->size chính là b->size (cùng offset)
            h->magic = MAGIC_USED;
            h->tag = tag;
            return h + 1;
        }
        if (!grow(need)) return nullptr;
    }
}

void kfree(void* p) {
    if (p == nullptr) return;
    auto* h = static_cast<Header*>(p) - 1;
    KASSERT(h->magic == MAGIC_USED, "kfree: con tro khong do kmalloc cap, hoac double free");
    SpinLockGuard guard(heap_lock);
    h->magic = 0;                                              // lần kfree thứ 2 sẽ trượt KASSERT ở trên
    insert_and_coalesce(reinterpret_cast<FreeBlock*>(h));
}

void* kmalloc_or_panic(size_t n, uint32_t tag) {
    void* p = kmalloc(n, tag);
    if (p == nullptr) panic("het bo nho kernel heap: %lu byte, tag %x", n, tag);
    return p;
}
```

**Kernel stack có guard page:**

```cpp
// kernel/mm/kstack.cpp — mọi stack cùng kích thước → tái sử dụng bằng danh sách stack trống
VirtAddr kstack_alloc(size_t pages) {
    // Lấy slot từ free list; nếu trống thì bump `kstack_next` thêm (pages + 1) trang.
    const VirtAddr slot = kstack_take_slot(pages + 1);
    for (size_t i = 1; i <= pages; i++) {                      // i = 0 là guard page: CỐ Ý không map
        const PhysAddr f = pmm_alloc_frame();
        KASSERT(f != 0, "het frame cho kernel stack");
        vmm_map_page(kernel_as, slot + i * PAGE_SIZE, f, PTE_WRITABLE | PTE_NX);
    }
    return slot + (pages + 1) * PAGE_SIZE;                     // đỉnh stack, bội 16 ✓
}
```

Tràn stack → ghi vào guard page → #PF → CPU không push được frame #PF (stack
hỏng) → #DF → IST1 → `panic_frame` in RSP nằm trong trang guard. Đây đúng là
lý do IST1 tồn tại từ đầu.

#### 5.5.5 Page fault handler

```cpp
// kernel/mm/fault.cpp
namespace {
Status resolve_user_fault(AddressSpace& as, VirtAddr addr, uint64_t err) {
    uint64_t pte_flags;
    {
        SpinLockGuard guard(as.lock);
        const VmArea* vma = vma_find(as, addr);
        if (vma == nullptr) return E_FAULT;                                    // không thuộc vùng nào
        if (err & PF_PRESENT) return E_FAULT;                                  // có trang nhưng sai quyền (chưa có COW)
        if ((err & PF_WRITE) && !(vma->flags & VM_WRITE)) return E_FAULT;
        if ((err & PF_INSTRUCTION) && !(vma->flags & VM_EXEC)) return E_FAULT;
        pte_flags = vma_to_pte_flags(*vma);
    }   // nhả khoá trước khi cấp frame: vmm_map_page tự lấy lại as.lock

    const PhysAddr frame = pmm_alloc_frame();
    if (frame == 0) return E_NOMEM;
    memset(reinterpret_cast<void*>(phys_to_virt(frame)), 0, PAGE_SIZE);        // demand-ZERO: không rò dữ liệu process khác
    const Status s = vmm_map_page(as, align_down(addr, PAGE_SIZE), frame, pte_flags);
    if (s == E_BUSY) { pmm_free_frame(frame); return OK; }                     // ai đó vừa map trước → coi như xong
    return s;
}
}  // namespace

void mm_page_fault(InterruptFrame* f) {
    const VirtAddr addr = read_cr2();                          // đọc NGAY: fault lồng nhau sẽ ghi đè CR2
    const uint64_t err = f->error_code;
    Cpu& cpu = this_cpu();
    const bool from_user = frame_from_user(f);

    if (!from_user && cpu.irql >= Irql::Dispatch)              // ≈ bugcheck IRQL_NOT_LESS_OR_EQUAL
        panic_frame(f, "page fault o IRQL >= DISPATCH, addr=%p err=%lx", reinterpret_cast<void*>(addr), err);
    if (err & PF_RESERVED)
        panic_frame(f, "page table hong (reserved bit), addr=%p", reinterpret_cast<void*>(addr));

    Process* proc = cpu.current ? cpu.current->process : nullptr;
    if (addr < USER_TOP && proc != nullptr) {
        if (f->rflags & RFLAGS_IF) cpu_enable_interrupts();   // ngữ cảnh gốc cho phép ngắt → xử lý cũng vậy
        const Status s = resolve_user_fault(*proc->address_space, addr, err);
        cpu_disable_interrupts();
        if (s == OK) return;                                   // KẾT CỤC 1 (mục 3.5)
    }
    if (from_user) {                                           // KẾT CỤC 2
        kprintf("pid %u: segmentation fault addr=%p rip=%p\n", proc ? proc->pid : 0,
                reinterpret_cast<void*>(addr), reinterpret_cast<void*>(f->rip));
        ps_terminate_current(E_FAULT);
    }
    if (const VirtAddr fixup = exception_table_lookup(f->rip)) {   // KẾT CỤC 2b: copy_from_user gặp địa chỉ xấu
        f->rip = fixup;
        return;
    }
    panic_frame(f, "kernel page fault addr=%p err=%lx", reinterpret_cast<void*>(addr), err);   // KẾT CỤC 3
}
```

#### 5.5.6 Truy cập bộ nhớ user an toàn (exception table)

**Vấn đề:** syscall nhận con trỏ từ user. Kiểm tra `is_user_range` chặn được
con trỏ trỏ vào kernel, nhưng không chặn được con trỏ nằm trong nửa user mà
chưa map. Kiểm tra VMA trước rồi mới copy cũng không đủ (thread khác của cùng
process có thể unmap giữa chừng). **Giải pháp chuẩn:** cứ copy; nếu #PF xảy ra
đúng tại lệnh copy, page fault handler tra bảng và "bẻ" RIP sang đoạn code trả
lỗi. Tương đương `__try/__except` + `ProbeForRead` của NT, nhưng không cần SEH.

```nasm
; kernel/arch/x86_64/user_copy.asm
; uint64_t copy_user_bytes(void* dst, const void* src, size_t n) → số byte CHƯA copy (0 = thành công)
BITS 64
section .text
global copy_user_bytes
copy_user_bytes:
    mov rcx, rdx
.copy:
    rep movsb               ; #PF có thể xảy ra ở đây, RIP báo lỗi = địa chỉ .copy
    xor eax, eax
    ret
.fault:
    mov rax, rcx            ; `rep` giữ RCX = số byte còn lại tại thời điểm fault
    ret

section .ex_table           ; linker.ld gom thành [__ex_table_start, __ex_table_end)
    dq copy_user_bytes.copy, copy_user_bytes.fault
```

```cpp
// kernel/mm/user_copy.hpp
struct ExTableEntry { VirtAddr fault_rip; VirtAddr fixup_rip; };
extern "C" const ExTableEntry __ex_table_start[], __ex_table_end[];
extern "C" uint64_t copy_user_bytes(void* dst, const void* src, size_t n);

inline VirtAddr exception_table_lookup(VirtAddr rip) {
    for (const ExTableEntry* e = __ex_table_start; e != __ex_table_end; ++e)
        if (e->fault_rip == rip) return e->fixup_rip;
    return 0;
}

inline Status copy_from_user(void* kernel_dst, uint64_t user_src, size_t n) {
    if (!is_user_range(user_src, n)) return E_FAULT;
    return copy_user_bytes(kernel_dst, reinterpret_cast<const void*>(user_src), n) == 0 ? OK : E_FAULT;
}

inline Status copy_to_user(uint64_t user_dst, const void* kernel_src, size_t n) {
    if (!is_user_range(user_dst, n)) return E_FAULT;
    return copy_user_bytes(reinterpret_cast<void*>(user_dst), kernel_src, n) == 0 ? OK : E_FAULT;
}

// Chuỗi kết thúc bằng NUL: copy từng byte (chậm nhưng đơn giản, đúng trước đã)
inline Status copy_string_from_user(char* dst, uint64_t user_src, size_t capacity) {
    for (size_t i = 0; i < capacity; i++) {
        const Status s = copy_from_user(&dst[i], user_src + i, 1);
        if (s != OK) return s;
        if (dst[i] == '\0') return OK;
    }
    return E_INVAL;                                            // dài quá capacity
}
```

> Nếu sau này chạy với `-cpu max` (có SMAP) và bật `CR4.SMAP`: phải bọc
> `rep movsb` bằng `stac`/`clac`, nếu không chính kernel sẽ #PF khi chạm trang user.

### 5.6 `ob/` — Object Manager rút gọn

**Vì sao cần, trước cả khi có file:** ngay khi có syscall, user sẽ cầm một
"thứ gì đó" đại diện cho tài nguyên kernel. Đưa thẳng con trỏ kernel cho user
thì vừa lộ địa chỉ, vừa không kiểm soát được thời gian sống. Handle giải
quyết cả hai: (1) một chỉ số vô nghĩa ngoài process đó, (2) kernel kiểm tra
loại + quyền ở mỗi lần dùng, (3) refcount giữ object sống khi thread A đang
`read()` mà thread B `close()` cùng handle.

```cpp
// kernel/ob/object.hpp
enum class ObjectType : uint8_t { Process = 1, Thread, File, Event, Device };

enum Access : uint32_t {             // ≈ ACCESS_MASK
    ACCESS_READ      = 1 << 0,
    ACCESS_WRITE     = 1 << 1,
    ACCESS_EXECUTE   = 1 << 2,
    ACCESS_WAIT      = 1 << 3,       // chờ process/thread kết thúc
    ACCESS_TERMINATE = 1 << 4,
};

struct ObjectHeader {                // ≈ OBJECT_HEADER (ở NT nằm TRƯỚC body; ở đây là field đầu của body)
    ObjectType type;
    uint32_t   ref_count;
    void     (*destroy)(ObjectHeader*);
};

void ob_init_header(ObjectHeader& h, ObjectType type, void (*destroy)(ObjectHeader*));   // ref_count = 1
void ob_reference(ObjectHeader* h);                                                     // ≈ ObReferenceObject
void ob_dereference(ObjectHeader* h);   // về 0 → destroy(). Lần deref cuối PHẢI ở PASSIVE (destroy có thể chờ mutex)

// kernel/ob/handle_table.hpp
using Handle = int32_t;
struct HandleEntry { ObjectHeader* object; uint32_t granted_access; };
struct HandleTable { HandleEntry* entries; uint32_t capacity; SpinLock lock; };

Status handle_table_init(HandleTable& t, uint32_t initial_capacity);
Status ob_insert_handle(HandleTable& t, ObjectHeader* obj, uint32_t access, Handle* out);   // tăng refcount
Status ob_close_handle(HandleTable& t, Handle h);
void   handle_table_close_all(HandleTable& t);                                              // khi process chết
```

```cpp
// kernel/ob/handle_table.cpp
// ≈ ObReferenceObjectByHandle: tra handle, kiểm tra loại + quyền, trả object ĐÃ tăng refcount.
Status ob_reference_by_handle(HandleTable& t, Handle h, ObjectType expected,
                              uint32_t desired_access, ObjectHeader** out) {
    SpinLockGuard guard(t.lock);
    if (h < 0 || static_cast<uint32_t>(h) >= t.capacity) return E_BADF;
    const HandleEntry& e = t.entries[h];
    if (e.object == nullptr || e.object->type != expected) return E_BADF;
    if ((e.granted_access & desired_access) != desired_access) return E_ACCESS;
    ob_reference(e.object);
    *out = e.object;
    return OK;
}

Status ob_close_handle(HandleTable& t, Handle h) {
    ObjectHeader* obj = nullptr;
    {
        SpinLockGuard guard(t.lock);
        if (h < 0 || static_cast<uint32_t>(h) >= t.capacity || t.entries[h].object == nullptr) return E_BADF;
        obj = t.entries[h].object;
        t.entries[h] = {nullptr, 0};
    }
    ob_dereference(obj);        // NGOÀI khoá: destroy có thể chờ KMutex — cấm làm khi đang giữ SpinLock
    return OK;
}
```

> **Ghi chú thiết kế:** NT kiểm tra quyền (ACL) **một lần** lúc mở, ghi
> `granted_access` vào handle, các lần dùng sau chỉ so bit — rẻ. Thiết kế
> này giữ đúng ý đó; chưa có ACL/user account nên quyền lúc mở được cấp theo
> cờ mở file.

### 5.7 `ps/` — process, syscall, user mode, ELF loader

#### 5.7.1 SYSCALL/SYSRET

```cpp
// kernel/include/kernel/syscall_numbers.hpp — dùng chung với libc
enum SyscallNumber : uint64_t {
    SYS_EXIT = 0, SYS_WRITE, SYS_READ, SYS_OPEN, SYS_CLOSE,
    SYS_SPAWN, SYS_WAIT, SYS_SLEEP_MS, SYS_YIELD, SYS_MMAP,
    SYS_COUNT
};
// Quy ước thanh ghi (giống Linux x86-64): rax = số hiệu; rdi, rsi, rdx, r10, r8, r9 = tham số.
// Dùng r10 thay rcx vì lệnh SYSCALL ghi đè rcx bằng RIP trả về.

// kernel/ps/syscall.cpp
void syscall_init() {
    wrmsr(MSR_EFER, rdmsr(MSR_EFER) | EFER_SCE);
    // STAR[47:32]: SYSCALL nạp CS = 0x08, SS = 0x08 + 8 = 0x10
    // STAR[63:48]: SYSRET  nạp SS = 0x18 + 8 = 0x20|3, CS = 0x18 + 16 = 0x28|3   (bảng 4.6)
    wrmsr(MSR_STAR, (uint64_t{USER_BASE_SELECTOR} << 48) | (uint64_t{KERNEL_CODE_SELECTOR} << 32));
    wrmsr(MSR_LSTAR, reinterpret_cast<uint64_t>(syscall_entry));
    // Cờ bị XOÁ khi vào kernel: IF (chưa có stack kernel), DF (ABI), TF (chống single-step vào kernel), AC, NT, IOPL
    wrmsr(MSR_FMASK, 0x47700);
}
```

```cpp
struct SyscallFrame {                 // thứ tự khớp CHÍNH XÁC chuỗi push trong syscall_entry
    uint64_t r15, r14, r13, r12, rbp, rbx;
    uint64_t r9, r8, r10, rdx, rsi, rdi;
    uint64_t rax;                     // vào: số hiệu syscall — ra: giá trị trả về
    uint64_t user_rip;                // RCX lúc SYSCALL
    uint64_t user_rflags;             // R11 lúc SYSCALL
    uint64_t user_rsp;
};
static_assert(sizeof(SyscallFrame) == 16 * 8);
```

```nasm
; kernel/arch/x86_64/syscall_entry.asm
; Lúc vào: CPL=0, CS=0x08, RIP=LSTAR, RCX=RIP user, R11=RFLAGS user, IF=0 (FMASK),
;          nhưng RSP VẪN LÀ STACK USER và GS VẪN LÀ GS USER. Hai lệnh đầu phải xử lý đúng việc đó.
CPU_KERNEL_RSP equ 8                  ; khớp static_assert offsetof(Cpu, ...) ở cpu.hpp
CPU_USER_RSP   equ 16

extern syscall_dispatch
global syscall_entry
syscall_entry:
    swapgs                            ; GS_BASE ← &cpu (kernel)
    mov [gs:CPU_USER_RSP], rsp        ; cất tạm RSP user vào per-CPU
    mov rsp, [gs:CPU_KERNEL_RSP]      ; sang kernel stack của thread hiện tại (bội 16)
    push qword [gs:CPU_USER_RSP]      ; user_rsp
    push r11                          ; user_rflags
    push rcx                          ; user_rip
    push rax
    push rdi
    push rsi
    push rdx
    push r10
    push r8
    push r9
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15                          ; 16 × 8 = 128 byte → RSP%16 == 0 trước `call` ✓
    cld
    mov rdi, rsp
    sti                               ; từ đây là IRQL PASSIVE thật sự: được ngắt, được ngủ, được preempt
    call syscall_dispatch
    cli                               ; từ đây tới sysret không được ngắt (sắp đổi về stack user)
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    pop r9
    pop r8
    pop r10
    pop rdx
    pop rsi
    pop rdi
    pop rax
    pop rcx                           ; RIP user
    pop r11                           ; RFLAGS user
    pop rsp                           ; ⚠ từ đây CPL=0 nhưng RSP là stack user → NMI/#MC phải dùng IST (4.7)
    swapgs                            ; GS_BASE ← GS user
    o64 sysret
```

```cpp
using SyscallFn = int64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

constexpr SyscallFn syscall_table[SYS_COUNT] = {
    sys_exit, sys_write, sys_read, sys_open, sys_close,
    sys_spawn, sys_wait, sys_sleep_ms, sys_yield, sys_mmap,
};

extern "C" void syscall_dispatch(SyscallFrame* f) {
    const uint64_t nr = f->rax;
    f->rax = nr < SYS_COUNT
        ? static_cast<uint64_t>(syscall_table[nr](f->rdi, f->rsi, f->rdx, f->r10, f->r8, f->r9))
        : static_cast<uint64_t>(E_NOSYS);

    // SYSRET với RCX không-canonical gây #GP TRONG ring 0 nhưng RSP đã là stack user
    // (lỗ hổng leo thang quyền kinh điển trên CPU Intel, CVE-2012-0217).
    // Chỉ xảy ra nếu kernel từng sửa user_rip (vd: signal, exec) — chặn ở một chỗ duy nhất này.
    if (f->user_rip >= USER_TOP) ps_terminate_current(E_FAULT);
}
```

#### 5.7.2 Ví dụ syscall handler (mẫu cho mọi handler)

```cpp
// kernel/io/sys_file.cpp — handler nằm trong thành phần sở hữu nó (NT: NtReadFile thuộc I/O Manager)
constexpr size_t MAX_IO_CHUNK = 64 * 1024;

int64_t sys_write(uint64_t handle, uint64_t user_buf, uint64_t len, uint64_t, uint64_t, uint64_t) {
    if (len == 0) return 0;
    if (len > MAX_IO_CHUNK) len = MAX_IO_CHUNK;           // ghi một phần là hợp lệ, libc tự lặp

    Process* p = ps_current_process();
    ObjectHeader* obj = nullptr;
    if (Status s = ob_reference_by_handle(p->handles, static_cast<Handle>(handle),
                                          ObjectType::File, ACCESS_WRITE, &obj); s != OK)
        return s;
    auto* file = reinterpret_cast<FileObject*>(obj);

    auto* kbuf = static_cast<uint8_t*>(kmalloc(len, TAG('W','r','i','t')));
    Status s = kbuf ? OK : E_NOMEM;
    if (s == OK) s = copy_from_user(kbuf, user_buf, len);  // ① copy vào kernel TRƯỚC — tránh TOCTOU
    size_t done = 0;
    if (s == OK) s = vfs_write(file, kbuf, len, &done);    // ② mọi tầng dưới chỉ thấy buffer kernel

    kfree(kbuf);
    ob_dereference(obj);
    return s == OK ? static_cast<int64_t>(done) : s;
}
```

#### 5.7.3 Process và vào ring 3

```cpp
// kernel/ps/process.hpp
struct Process {                      // ≈ EPROCESS
    ObjectHeader  header;
    uint32_t      pid;
    AddressSpace* address_space;
    HandleTable   handles;
    ListNode      threads;
    Process*      parent;
    Status        exit_code;
    KEvent        exited;             // Notification event: sys_wait chờ trên đây (≈ process object "signaled" khi kết thúc)
    char          name[32];
};
```

```nasm
; kernel/arch/x86_64/user_entry.asm
; [[noreturn]] void enter_user_mode(uint64_t rip, uint64_t rsp) — gọi với IF=0
global enter_user_mode
enter_user_mode:
    push 0x23               ; SS  = user data  (0x20) | RPL 3
    push rsi                ; RSP user
    push 0x202              ; RFLAGS: IF=1, bit 1 luôn = 1
    push 0x2B               ; CS  = user code64 (0x28) | RPL 3
    push rdi                ; RIP user
    xor eax, eax            ; xoá mọi thanh ghi: không rò địa chỉ/dữ liệu kernel sang user
    xor ebx, ebx
    xor ecx, ecx
    xor edx, edx
    xor esi, esi
    xor edi, edi
    xor ebp, ebp
    xor r8d, r8d
    xor r9d, r9d
    xor r10d, r10d
    xor r11d, r11d
    xor r12d, r12d
    xor r13d, r13d
    xor r14d, r14d
    xor r15d, r15d
    swapgs                  ; GS_BASE ← user (0)
    iretq                   ; CPU thấy CS RPL=3 → nạp cả SS:RSP → ring 3
```

```cpp
// kernel/ps/process.cpp
constexpr VirtAddr USER_STACK_TOP  = 0x00007FFFFFFFE000ULL;
constexpr uint64_t USER_STACK_SIZE = 8ULL << 20;

struct UserStart { VirtAddr entry; VirtAddr rsp; };

void user_thread_entry(void* arg) {          // chạy ở PASSIVE, CR3 đã là của process (sched 5.4.5)
    const UserStart start = *static_cast<UserStart*>(arg);
    delete static_cast<UserStart*>(arg);
    cpu_disable_interrupts();                // iretq sẽ bật lại IF theo RFLAGS 0x202
    enter_user_mode(start.entry, start.rsp);
}

Status ps_spawn(const char* path, Process* parent, Process** out) {
    FileObject* file = nullptr;
    if (Status s = vfs_open(path, ACCESS_READ | ACCESS_EXECUTE, &file); s != OK) return s;

    AddressSpace* as = vmm_create_address_space();
    if (as == nullptr) { ob_dereference(&file->header); return E_NOMEM; }

    VirtAddr entry = 0;
    Status s = elf_load(file->vnode, *as, &entry);
    ob_dereference(&file->header);
    if (s == OK) s = vma_add(*as, USER_STACK_TOP - USER_STACK_SIZE, USER_STACK_TOP, VM_READ | VM_WRITE);
    if (s != OK) { vmm_destroy_address_space(as); return s; }

    auto* p = new Process{};
    ob_init_header(p->header, ObjectType::Process, process_destroy);
    p->pid = next_pid++;
    p->address_space = as;
    p->parent = parent;
    list_init(&p->threads);
    ke_event_init(p->exited, EventType::Notification, false);
    copy_name(p->name, path);
    handle_table_init(p->handles, 16);
    if (parent) handle_table_inherit_stdio(p->handles, parent->handles);   // handle 0,1,2
    else        open_console_as_stdio(p->handles);                         // /dev/tty0 × 3

    // RSP = đỉnh - 16: bội 16 ✓ đúng ABI tại _start; [rsp] = argc = 0 và [rsp+8] = argv[0] = NULL
    // mà không cần ghi gì — trang stack là demand-zero (5.5.5).
    auto* start = new UserStart{entry, USER_STACK_TOP - 16};
    ke_create_thread(p, user_thread_entry, start, p->name);
    *out = p;                                                  // caller giữ 1 reference
    return OK;
}

[[noreturn]] void ps_terminate_current(Status code) {
    Process* p = ps_current_process();
    KASSERT(p != nullptr, "kernel thread khong terminate kieu nay");
    cpu_enable_interrupts();                  // có thể tới từ exception (IF=0) — đóng handle cần được chờ
    p->exit_code = code;
    handle_table_close_all(p->handles);
    ke_event_set(p->exited);                  // đánh thức sys_wait của process cha
    ke_exit_thread();                         // address space được giải phóng khi refcount process về 0
}
```

#### 5.7.4 ELF loader

```cpp
// kernel/ps/elf_loader.cpp
struct Elf64_Ehdr {
    uint8_t  e_ident[16];
    uint16_t e_type, e_machine;
    uint32_t e_version;
    uint64_t e_entry, e_phoff, e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} __attribute__((packed));
struct Elf64_Phdr {
    uint32_t p_type, p_flags;
    uint64_t p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align;
} __attribute__((packed));
static_assert(sizeof(Elf64_Ehdr) == 64 && sizeof(Elf64_Phdr) == 56);

constexpr uint16_t ET_EXEC = 2, EM_X86_64 = 62;
constexpr uint32_t PT_LOAD = 1, PF_X = 1, PF_W = 2;

Status elf_load(Vnode* file, AddressSpace& as, VirtAddr* entry_out) {
    Elf64_Ehdr eh;
    if (vnode_read_exact(file, 0, &eh, sizeof eh) != OK) return E_NOEXEC;
    if (memcmp(eh.e_ident, "\x7F" "ELF", 4) != 0 || eh.e_ident[4] != 2 /*64-bit*/ || eh.e_ident[5] != 1 /*LE*/ ||
        eh.e_type != ET_EXEC || eh.e_machine != EM_X86_64 ||
        eh.e_phentsize != sizeof(Elf64_Phdr) || eh.e_phnum == 0 || eh.e_phnum > 32)
        return E_NOEXEC;

    for (uint16_t i = 0; i < eh.e_phnum; i++) {
        Elf64_Phdr ph;
        if (vnode_read_exact(file, eh.e_phoff + uint64_t{i} * sizeof ph, &ph, sizeof ph) != OK) return E_NOEXEC;
        if (ph.p_type != PT_LOAD || ph.p_memsz == 0) continue;

        // File ELF là DỮ LIỆU KHÔNG ĐÁNG TIN: kiểm tra mọi con số trước khi dùng
        if (ph.p_filesz > ph.p_memsz || ph.p_vaddr < USER_BASE || !is_user_range(ph.p_vaddr, ph.p_memsz))
            return E_NOEXEC;

        const VirtAddr seg_start = align_down(ph.p_vaddr, PAGE_SIZE);
        const VirtAddr seg_end   = align_up(ph.p_vaddr + ph.p_memsz, PAGE_SIZE);
        const uint32_t vm_flags  = VM_READ | ((ph.p_flags & PF_W) ? VM_WRITE : 0) | ((ph.p_flags & PF_X) ? VM_EXEC : 0);
        if (Status s = vma_add(as, seg_start, seg_end, vm_flags); s != OK) return s;   // 2 segment chung 1 trang → từ chối
        const uint64_t pte_flags = vma_to_pte_flags(VmArea{{}, seg_start, seg_end, vm_flags});

        const VirtAddr file_lo = ph.p_vaddr;
        const VirtAddr file_hi = ph.p_vaddr + ph.p_filesz;
        for (VirtAddr page = seg_start; page < seg_end; page += PAGE_SIZE) {
            const PhysAddr frame = pmm_alloc_frame();
            if (frame == 0) return E_NOMEM;                    // caller huỷ address space → dọn các frame đã map
            auto* dst = reinterpret_cast<uint8_t*>(phys_to_virt(frame));
            memset(dst, 0, PAGE_SIZE);                         // phần .bss (memsz > filesz) = 0 sẵn

            // Ghi qua HHDM: không cần đổi CR3 sang address space đang dựng
            const VirtAddr lo = page > file_lo ? page : file_lo;
            const VirtAddr hi = page + PAGE_SIZE < file_hi ? page + PAGE_SIZE : file_hi;
            if (lo < hi && vnode_read_exact(file, ph.p_offset + (lo - file_lo), dst + (lo - page), hi - lo) != OK) {
                pmm_free_frame(frame);
                return E_NOEXEC;
            }
            if (Status s = vmm_map_page(as, page, frame, pte_flags); s != OK) {
                pmm_free_frame(frame);
                return s;
            }
        }
    }
    if (eh.e_entry < USER_BASE || eh.e_entry >= USER_TOP) return E_NOEXEC;
    *entry_out = eh.e_entry;
    return OK;
}
```

> Phiên bản 1 nạp **eager** (cấp và copy mọi trang ngay). Phiên bản 2 nạp
> **lazy**: VMA ghi nhớ (file, offset), page fault mới đọc trang — đó là
> "memory-mapped file" và là lúc page cache (CLAUDE.md đã bàn) trở nên cần thiết.

### 5.8 `io/` và `drivers/`

#### 5.8.1 Mô hình driver (≈ WDM rút gọn)

```cpp
// kernel/io/driver.hpp
enum class IrpMajor : uint8_t { Read, Write, DeviceControl, Count };   // ≈ IRP_MJ_READ/WRITE/DEVICE_CONTROL

struct Irp {                          // ≈ IRP, bản đồng bộ
    IrpMajor major;
    uint8_t* buffer;                  // LUÔN là bộ nhớ kernel (quy tắc 2.2-4)
    size_t   length;
    uint64_t offset;                  // byte offset — thiết bị ký tự bỏ qua
    uint32_t ioctl_code;
    size_t   information;             // ≈ IoStatus.Information: số byte đã xử lý thật
};

struct DeviceObject;
using DispatchRoutine = Status (*)(DeviceObject*, Irp*);

struct DriverObject {                 // ≈ DRIVER_OBJECT
    const char*     name;
    DispatchRoutine major_function[static_cast<size_t>(IrpMajor::Count)];
};

enum class DeviceType : uint8_t { Character, Block };

struct DeviceObject {                 // ≈ DEVICE_OBJECT
    ObjectHeader  header;
    DriverObject* driver;
    DeviceObject* lower;              // device stack: filter → function driver (≈ IoAttachDeviceToDeviceStack)
    DeviceType    type;
    uint32_t      block_size;
    uint64_t      block_count;
    char          name[16];           // tên trong /dev
    void*         extension;          // ≈ DeviceExtension
    ListNode      registry_link;
};

// kernel/io/driver.cpp
Status io_create_device(DriverObject* drv, const char* name, DeviceType type,
                        size_t extension_size, DeviceObject** out);   // kmalloc device + extension, thêm vào registry
DeviceObject* io_find_device(const char* name);

Status io_call_driver(DeviceObject* dev, Irp* irp) {                  // ≈ IoCallDriver
    const DispatchRoutine fn = dev->driver->major_function[static_cast<size_t>(irp->major)];
    return fn ? fn(dev, irp) : E_NOSYS;
}

// Filter driver chỉ cần xử lý phần của mình rồi chuyển xuống:
Status upper_filter_write(DeviceObject* dev, Irp* irp) {
    to_uppercase(irp->buffer, irp->length);        // ví dụ: filter viết hoa mọi thứ ghi ra tty
    return io_call_driver(dev->lower, irp);        // ≈ IoSkipCurrentIrpStackLocation + IoCallDriver
}
```

> **Bài tập nên làm khi tới M6:** viết đúng filter viết hoa ở trên, gắn lên
> `tty0`. Đó là phiên bản thu nhỏ của mẫu KbFiltr/filter driver trong sách
> Windows Kernel Programming, và cho thấy vì sao NT tổ chức driver thành stack.

**Nâng cấp sau:** I/O bất đồng bộ — dispatch trả `E_PENDING`, driver gọi
`io_complete_request(irp)` từ DPC khi phần cứng xong, người gọi chờ trên
`KEvent` trong IRP. ATA với IRQ14 là chỗ đầu tiên cần tới.

#### 5.8.2 Serial COM1

```cpp
// kernel/drivers/serial.cpp — polling; đủ cho log và panic
constexpr uint16_t COM1 = 0x3F8;

void serial_init() {
    outb(COM1 + 1, 0x00);    // tắt ngắt của UART
    outb(COM1 + 3, 0x80);    // DLAB = 1: 2 port đầu thành thanh ghi divisor
    outb(COM1 + 0, 0x01);    // divisor = 1 → 115200 baud
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);    // 8 bit, không parity, 1 stop bit; DLAB = 0
    outb(COM1 + 2, 0xC7);    // bật FIFO, xoá, ngưỡng 14 byte
    outb(COM1 + 4, 0x0B);    // DTR + RTS + OUT2
}

void serial_putc(char c) {
    while ((inb(COM1 + 5) & 0x20) == 0) {}   // LSR bit 5: thanh ghi truyền trống
    outb(COM1, static_cast<uint8_t>(c));
}
```

#### 5.8.3 Bàn phím PS/2 — mẫu ISR + DPC

```cpp
// kernel/drivers/keyboard.cpp
namespace {
constexpr uint32_t RING_SIZE = 256;          // lũy thừa 2
uint8_t  ring[RING_SIZE];
uint32_t ring_head = 0;                      // CHỈ ISR ghi
uint32_t ring_tail = 0;                      // CHỈ DPC ghi → không cần khoá (1 người ghi mỗi đầu)
Dpc      kbd_dpc;
bool     shift_down = false;

void keyboard_isr(InterruptFrame*, void*) {  // IRQL DEVICE — việc tối thiểu
    const uint8_t sc = inb(0x60);            // BẮT BUỘC đọc, nếu không controller không phát IRQ tiếp
    const uint32_t next = (ring_head + 1) & (RING_SIZE - 1);
    if (next != ring_tail) {                 // đầy thì bỏ phím — ISR không bao giờ được chờ
        ring[ring_head] = sc;
        ring_head = next;
    }
    ke_queue_dpc(kbd_dpc);
}

void keyboard_dpc(Dpc*, void*) {             // IRQL DISPATCH — dịch scancode, đẩy lên tty
    while (ring_tail != ring_head) {
        const uint8_t sc = ring[ring_tail];
        ring_tail = (ring_tail + 1) & (RING_SIZE - 1);
        if (sc == 0x2A || sc == 0x36) { shift_down = true;  continue; }   // Shift nhấn (set 1)
        if (sc == 0xAA || sc == 0xB6) { shift_down = false; continue; }   // Shift nhả
        if (sc & 0x80) continue;                                          // nhả phím khác
        const char c = (shift_down ? SCANCODE_SET1_SHIFT : SCANCODE_SET1)[sc];
        if (c != 0) tty_input_char(tty0, c);
    }
}
}  // namespace

void keyboard_init() {
    dpc_setup(kbd_dpc, keyboard_dpc, nullptr);
    irq_register(1, keyboard_isr, nullptr);
}
```

#### 5.8.4 TTY — nơi gặp nhau của input và output

```cpp
// kernel/drivers/tty.cpp
struct Tty {
    char     editing[256];  size_t editing_len;   // dòng đang gõ, chưa Enter
    char     ready[1024];   size_t ready_len;     // byte đã hoàn chỉnh, chờ read()
    KEvent   data_available;                      // Notification: signaled khi ready_len > 0
    SpinLock lock;
};

void tty_input_char(Tty& t, char c) {             // từ keyboard DPC, IRQL DISPATCH
    SpinLockGuard guard(t.lock);
    if (c == '\b') {
        if (t.editing_len > 0) { t.editing_len--; console_write("\b \b", 3); }
        return;
    }
    if (t.editing_len < sizeof t.editing) { t.editing[t.editing_len++] = c; console_write(&c, 1); }
    if (c == '\n') {
        const size_t room = sizeof t.ready - t.ready_len;
        const size_t n = t.editing_len < room ? t.editing_len : room;
        memcpy(t.ready + t.ready_len, t.editing, n);
        t.ready_len += n;
        t.editing_len = 0;
        ke_event_set(t.data_available);           // hợp lệ ở DISPATCH (bảng 4.8)
    }
}

Status tty_read(Tty& t, char* buf, size_t capacity, size_t* done) {   // IRQL PASSIVE
    for (;;) {
        {
            SpinLockGuard guard(t.lock);
            if (t.ready_len > 0) {
                const size_t n = t.ready_len < capacity ? t.ready_len : capacity;
                memcpy(buf, t.ready, n);
                memmove(t.ready, t.ready + n, t.ready_len - n);
                t.ready_len -= n;
                if (t.ready_len == 0) ke_event_reset(t.data_available);
                *done = n;
                return OK;
            }
        }
        ke_event_wait(t.data_available);          // ngủ NGOÀI khoá
    }
}

// DriverObject của tty: Read → tty_read, Write → console_write (vga + serial)
```

#### 5.8.5 ATA PIO (M7)

```cpp
// kernel/drivers/ata.cpp — primary channel, LBA28, polling
constexpr uint16_t ATA_DATA = 0x1F0, ATA_SECCOUNT = 0x1F2, ATA_LBA0 = 0x1F3, ATA_LBA1 = 0x1F4,
                   ATA_LBA2 = 0x1F5, ATA_DRIVE = 0x1F6, ATA_CMD_STATUS = 0x1F7, ATA_ALT_STATUS = 0x3F6;
constexpr uint8_t  ST_ERR = 0x01, ST_DRQ = 0x08, ST_DF = 0x20, ST_BSY = 0x80;
constexpr uint8_t  CMD_READ_SECTORS = 0x20;

struct AtaDrive { bool slave; uint64_t sectors; KMutex lock; };

namespace {
Status wait_for_data() {
    for (int i = 0; i < 4; i++) inb(ATA_ALT_STATUS);          // trễ ~400ns: status cũ chưa kịp cập nhật
    for (uint32_t spin = 0; spin < 1'000'000; spin++) {
        const uint8_t st = inb(ATA_CMD_STATUS);
        if (st & ST_BSY) continue;
        if (st & (ST_ERR | ST_DF)) return E_IO;
        if (st & ST_DRQ) return OK;
    }
    return E_IO;                                              // timeout
}
}  // namespace

Status ata_read_sectors(AtaDrive& d, uint32_t lba, uint8_t count, uint8_t* buf) {   // count 1..255, PASSIVE
    KASSERT(count > 0 && lba < (1u << 28), "ata: tham so LBA28 khong hop le");
    ke_mutex_acquire(d.lock);                                 // thanh ghi ATA là trạng thái dùng chung
    outb(ATA_DRIVE, static_cast<uint8_t>(0xE0 | (d.slave ? 0x10 : 0) | ((lba >> 24) & 0x0F)));   // chế độ LBA
    outb(ATA_SECCOUNT, count);
    outb(ATA_LBA0, lba & 0xFF);
    outb(ATA_LBA1, (lba >> 8) & 0xFF);
    outb(ATA_LBA2, (lba >> 16) & 0xFF);
    outb(ATA_CMD_STATUS, CMD_READ_SECTORS);
    Status s = OK;
    for (uint8_t i = 0; i < count && s == OK; i++) {
        s = wait_for_data();
        if (s == OK) insw(ATA_DATA, reinterpret_cast<uint16_t*>(buf + size_t{i} * 512), 256);
    }
    ke_mutex_release(d.lock);
    return s;
}
```

### 5.9 `fs/` — VFS, tarfs, block cache, FAT32

#### 5.9.1 VFS

VFS là **một lớp trừu tượng duy nhất** giữa syscall và mọi filesystem/thiết
bị: `sys_read` không biết đầu bên kia là tar trong RAM, file FAT32 hay bàn
phím.

```cpp
// kernel/fs/vfs.hpp
enum class VnodeType : uint8_t { File, Directory, Device };
struct DirEntry { char name[64]; VnodeType type; uint64_t size; };

class Vnode {                                   // ≈ inode (Linux) / FCB (NT)
public:
    virtual ~Vnode() = default;
    virtual Status read(uint64_t offset, void* buf, size_t len, size_t* done) = 0;
    virtual Status write(uint64_t offset, const void* buf, size_t len, size_t* done) = 0;
    virtual Status lookup(const char* name, size_t name_len, Vnode** out) = 0;   // trả vnode đã ref
    virtual Status readdir(uint64_t index, DirEntry* out) = 0;

    VnodeType type;
    uint64_t  size = 0;
    uint32_t  refs = 1;
    Vnode*    mounted_here = nullptr;           // khác null: một filesystem khác được mount lên thư mục này
};
void vnode_ref(Vnode* v);
void vnode_unref(Vnode* v);                     // về 0 → delete (PASSIVE)

struct FileObject {                             // ≈ FILE_OBJECT: 1 lần open = 1 FileObject
    ObjectHeader header;
    Vnode*       vnode;
    uint64_t     offset;                        // vị trí đọc/ghi hiện tại — riêng từng lần open
    uint32_t     access;
    KMutex       lock;                          // bảo vệ offset khi 2 thread dùng chung handle
};

Status vfs_mount(const char* path, Vnode* fs_root);
Status vfs_lookup(const char* path, Vnode** out);
Status vfs_open(const char* path, uint32_t access, FileObject** out);
Status vfs_read(FileObject* f, void* buf, size_t len, size_t* done);
Status vfs_write(FileObject* f, const void* buf, size_t len, size_t* done);
```

```cpp
// kernel/fs/path.cpp
Status vfs_lookup(const char* path, Vnode** out) {
    if (path == nullptr || path[0] != '/') return E_INVAL;       // phiên bản 1: chưa có thư mục làm việc
    Vnode* cur = root_vnode;
    vnode_ref(cur);
    const char* p = path;
    for (;;) {
        while (*p == '/') p++;
        if (*p == '\0') break;
        const char* name = p;
        while (*p != '\0' && *p != '/') p++;
        const size_t len = static_cast<size_t>(p - name);

        if (cur->type != VnodeType::Directory) { vnode_unref(cur); return E_NOTDIR; }
        Vnode* next = nullptr;
        const Status s = cur->lookup(name, len, &next);
        vnode_unref(cur);
        if (s != OK) return s;
        while (next->mounted_here != nullptr) {                   // đi xuyên mount point: /disk → root FAT32
            Vnode* mounted = next->mounted_here;
            vnode_ref(mounted);
            vnode_unref(next);
            next = mounted;
        }
        cur = next;
    }
    *out = cur;
    return OK;
}

Status vfs_read(FileObject* f, void* buf, size_t len, size_t* done) {
    ke_mutex_acquire(f->lock);
    Status s = f->vnode->read(f->offset, buf, len, done);
    if (s == OK) f->offset += *done;
    ke_mutex_release(f->lock);
    return s;
}
```

`devfs`: thư mục ảo mà `lookup("tty0")` trả `DeviceVnode` bọc `DeviceObject`;
`DeviceVnode::read` dựng `Irp{Read}` rồi `io_call_driver` — đây là điểm nối
giữa `fs/` và `io/` trong luồng 3.4.

#### 5.9.2 tarfs — root filesystem từ initrd

```cpp
// kernel/fs/tarfs.cpp — đọc USTAR tại chỗ trong RAM (qua HHDM), chỉ đọc
struct TarHeader {
    char name[100]; char mode[8]; char uid[8]; char gid[8];
    char size[12];  char mtime[12]; char checksum[8]; char typeflag;
    char linkname[100]; char magic[6]; char version[2];
    char uname[32]; char gname[32]; char devmajor[8]; char devminor[8];
    char prefix[155]; char pad[12];
} __attribute__((packed));
static_assert(sizeof(TarHeader) == 512);

namespace {
uint64_t parse_octal(const char* s, size_t n) {
    uint64_t v = 0;
    for (size_t i = 0; i < n && s[i] >= '0' && s[i] <= '7'; i++) v = v * 8 + static_cast<uint64_t>(s[i] - '0');
    return v;
}
}

Status tarfs_mount_root(PhysAddr initrd_phys, uint64_t initrd_size) {
    const auto* base = reinterpret_cast<const uint8_t*>(phys_to_virt(initrd_phys));
    auto* root = new TarDirVnode("/");
    for (uint64_t off = 0; off + 512 <= initrd_size;) {
        const auto* h = reinterpret_cast<const TarHeader*>(base + off);
        if (h->name[0] == '\0') break;                               // block rỗng = hết archive
        if (memcmp(h->magic, "ustar", 5) != 0) return E_INVAL;
        const uint64_t size = parse_octal(h->size, sizeof h->size);
        if (size > initrd_size - off - 512) return E_INVAL;          // header nói dối về kích thước
        root->add_path(h->name, h->typeflag == '5' ? VnodeType::Directory : VnodeType::File,
                       base + off + 512, size);                      // tạo thư mục trung gian nếu cần
        off += 512 + align_up(size, 512);
    }
    return vfs_mount("/", root);
}
```

#### 5.9.3 Block cache

```cpp
// kernel/fs/block_cache.hpp — cache theo (thiết bị, số block), LRU, đếm tham chiếu
struct Buffer {
    DeviceObject* dev;
    uint64_t      block;
    uint8_t*      data;            // block_size byte
    uint32_t      refs;
    bool          valid;           // đã đọc từ đĩa chưa
    bool          dirty;           // đã sửa, chưa ghi xuống
    KMutex        lock;            // giữ trong lúc đọc/ghi nội dung
    ListNode      lru_link;
};

Buffer* bcache_get(DeviceObject* dev, uint64_t block);   // trả buffer ĐÃ lock + valid; miss → Irp{Read} → io_call_driver
void    bcache_mark_dirty(Buffer* b);
void    bcache_release(Buffer* b);                        // unlock, refs--, refs==0 → về đầu LRU (chưa bị đuổi)
Status  bcache_sync(DeviceObject* dev);                   // ghi mọi buffer dirty
```

Khi hết buffer, đuổi buffer `refs == 0` ở cuối LRU (ghi xuống trước nếu
`dirty`). Đây là tầng làm cho luồng 3.6 không đọc đĩa lại mỗi lần lookup.

#### 5.9.4 FAT32

```cpp
// kernel/fs/fat32/fat32.cpp — các offset theo đặc tả Microsoft FAT32
struct Fat32Volume {
    DeviceObject* dev;
    uint32_t bytes_per_sector;     // BPB @11 (u16) — chỉ hỗ trợ 512
    uint32_t sectors_per_cluster;  // BPB @13 (u8)
    uint32_t reserved_sectors;     // BPB @14 (u16)
    uint32_t num_fats;             // BPB @16 (u8)
    uint32_t sectors_per_fat;      // BPB @36 (u32)
    uint32_t root_cluster;         // BPB @44 (u32)
    uint32_t first_data_sector;    // = reserved + num_fats * sectors_per_fat
};

inline uint64_t cluster_to_lba(const Fat32Volume& v, uint32_t cluster) {
    return v.first_data_sector + uint64_t{cluster - 2} * v.sectors_per_cluster;   // cluster đánh số từ 2
}

Status fat_next_cluster(Fat32Volume& v, uint32_t cluster, uint32_t* next) {
    const uint64_t fat_offset = uint64_t{cluster} * 4;
    Buffer* b = bcache_get(v.dev, v.reserved_sectors + fat_offset / 512);
    if (b == nullptr) return E_IO;
    uint32_t entry;
    memcpy(&entry, b->data + fat_offset % 512, 4);
    bcache_release(b);
    *next = entry & 0x0FFFFFFF;                       // 4 bit cao là reserved
    return OK;                                        // *next >= 0x0FFFFFF8 → hết chuỗi
}

// Directory entry 32 byte: name[11] @0, attr @11, cluster_high @20 (u16), cluster_low @26 (u16), size @28 (u32)
// name[0] == 0x00 → hết thư mục; 0xE5 → đã xoá; attr == 0x0F → Long File Name (phiên bản 1: bỏ qua, chỉ tên 8.3)
class FatVnode : public Vnode { /* read: đi theo chuỗi cluster; lookup: duyệt entry 32B, so tên 8.3 */ };
```

### 5.10 Userland và libc

```nasm
; libc/crt0.asm — điểm vào mọi chương trình user
BITS 64
extern main
extern exit
global _start
_start:
    xor ebp, ebp            ; rbp = 0: đánh dấu đáy chuỗi frame cho debugger
    mov rdi, [rsp]          ; argc
    lea rsi, [rsp + 8]      ; argv
    call main               ; RSP%16 == 0 trước call ✓ (kernel đặt RSP = đỉnh stack - 16)
    mov rdi, rax
    call exit               ; không return

; libc/syscall.asm — cầu nối ABI hàm C (rdi, rsi, rdx, rcx, r8, r9) sang ABI syscall (rax, rdi, rsi, rdx, r10, r8)
global __syscall5
__syscall5:                 ; long __syscall5(long nr, long a1, long a2, long a3, long a4, long a5)
    mov rax, rdi
    mov rdi, rsi
    mov rsi, rdx
    mov rdx, rcx
    mov r10, r8
    mov r8, r9
    syscall
    ret
```

```c
/* libc/unistd.c */
long write(int fd, const void* buf, unsigned long n) { return __syscall5(SYS_WRITE, fd, (long)buf, (long)n, 0, 0); }
long read(int fd, void* buf, unsigned long n)        { return __syscall5(SYS_READ,  fd, (long)buf, (long)n, 0, 0); }
void exit(int code)                                  { __syscall5(SYS_EXIT, code, 0, 0, 0, 0); for (;;) {} }
int  spawn(const char* path)                         { return (int)__syscall5(SYS_SPAWN, (long)path, 0, 0, 0, 0); }  /* trả HANDLE tới process (kiểu CreateProcess) */
long wait(int handle)                                { return __syscall5(SYS_WAIT, handle, 0, 0, 0, 0); }            /* ≈ WaitForSingleObject + GetExitCodeProcess */
int  close(int handle)                               { return (int)__syscall5(SYS_CLOSE, handle, 0, 0, 0, 0); }
```

```c
/* userland/shell/main.c — vòng lặp tối giản */
int main(void) {
    char line[128];
    for (;;) {
        write(1, "> ", 2);
        long n = read(0, line, sizeof line - 1);
        if (n <= 0) continue;
        line[n - 1] = '\0';                                /* bỏ '\n' */
        if (line[0] == '\0') continue;
        int h = spawn(line);                               /* vd: "/bin/hello" */
        if (h < 0) { write(1, "khong chay duoc\n", 16); continue; }
        wait(h);
        close(h);                                          /* không close → Process object không bao giờ về refcount 0 → rò address space */
    }
}
```

**Cờ build chương trình user:**

```make
USER_CFLAGS  = -ffreestanding -nostdlib -static -fno-pie -fno-stack-protector \
               -mgeneral-regs-only -O2 -g -Ilibc/include -Ikernel/include
USER_LDFLAGS = -nostdlib -static -no-pie -Wl,-Ttext-segment=0x400000 \
               -Wl,-z,max-page-size=4096 -Wl,--build-id=none
```

> **Vì sao user cũng `-mgeneral-regs-only`:** kernel chưa lưu thanh ghi
> XMM/x87 khi context switch và chưa bật `CR4.OSFXSR`. Một chương trình user
> dùng SSE sẽ #UD, hoặc (sau khi bật OSFXSR) làm hỏng dữ liệu SSE của process
> khác. Hỗ trợ FPU/SSE thật (FXSAVE/XSAVE, lazy switch qua #NM — lịch sử
> thú vị của Windows) là một bước riêng ở M9.

---

## 6. Build, test, debug

### 6.1 Cờ compiler kernel

```make
# kernel/Makefile
CXXFLAGS = -std=c++17 -ffreestanding -fno-exceptions -fno-rtti \
           -fno-threadsafe-statics \
           -fno-stack-protector \
           -fno-asynchronous-unwind-tables -fcf-protection=none \
           -mno-red-zone -mcmodel=kernel -fno-pic -fno-pie \
           -mgeneral-regs-only \
           -fno-omit-frame-pointer \
           -O2 -g \
           -Wall -Wextra -Werror=return-type \
           -Iinclude -I. -Iarch/x86_64 \
           -MMD -MP
LDFLAGS  = -m elf_x86_64 -T linker.ld -nostdlib -z max-page-size=0x1000
# KHÔNG link libgcc của host: nó được build với red zone + SSE.
```

| Cờ | Vì sao |
|---|---|
| `-fno-threadsafe-statics` | Biến `static` cục bộ có constructor sẽ gọi `__cxa_guard_acquire` (cần thread library) |
| `-fno-stack-protector` | Ubuntu/Debian bật mặc định → cần `__stack_chk_fail` và canary ở `%fs:0x28` (FS base = 0 → đọc rác) |
| `-fno-asynchronous-unwind-tables` | Không sinh `.eh_frame` vô dụng (không có exception) |
| `-fcf-protection=none` | Không chèn `endbr64` (vô hại nhưng làm rối disassembly khi học) |
| `-mgeneral-regs-only` | Cấm SSE/x87/MMX: ISR và context switch không lưu thanh ghi XMM. Hiện ở `-O0` nên chưa lộ; bật `-O2` là g++ dùng XMM để copy struct ngay |
| `-fno-omit-frame-pointer` | `backtrace()` đi theo chuỗi RBP |
| `-O2 -g` | Build debug riêng: `make OPT=-O0`. Nên test ở cả hai vì `-O2` làm lộ UB (thiếu `volatile` cho MMIO…) |
| `-MMD -MP` | Thay danh sách `HEADERS` viết tay: tự sinh phụ thuộc header |

### 6.2 `linker.ld` higher-half

```ld
ENTRY(_start)
KERNEL_VMA  = 0xFFFFFFFF80000000;
KERNEL_PHYS = 0x100000;

SECTIONS
{
    . = KERNEL_VMA + KERNEL_PHYS;
    __kernel_start = .;

    .text : AT(ADDR(.text) - KERNEL_VMA) {
        KEEP(*(.boot_header))                 /* 4.3 — PHẢI là byte đầu tiên của kernel.bin */
        __text_start = .;
        *(.text .text.*)
        __text_end = .;
    }

    . = ALIGN(4096);                          /* ranh giới trang giữa các section → W^X theo trang */
    .rodata : AT(ADDR(.rodata) - KERNEL_VMA) {
        __rodata_start = .;
        *(.rodata .rodata.*)
        . = ALIGN(8);
        __ex_table_start = .;  KEEP(*(.ex_table))  __ex_table_end = .;   /* 5.5.6 */
        . = ALIGN(8);
        __ktests_start = .;    KEEP(*(.ktests))    __ktests_end = .;     /* 5.3.5 */
        __rodata_end = .;
    }

    . = ALIGN(4096);
    .data : AT(ADDR(.data) - KERNEL_VMA) {
        __data_start = .;
        . = ALIGN(8);
        __init_array_start = .;                                          /* 5.3.1 */
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        __init_array_end = .;
        *(.data .data.* .data.rel.ro .data.rel.ro.*)
    }
    __kernel_file_end = .;                    /* objcopy -O binary dừng ở đây (.bss không có trong file) */

    .bss : AT(ADDR(.bss) - KERNEL_VMA) {
        __bss_start = .;
        *(COMMON)
        *(.bss .bss.*)
        __bss_end = .;
    }
    . = ALIGN(4096);
    __kernel_end = .;

    __kernel_file_size = __kernel_file_end - __kernel_start;
    __kernel_mem_size  = __kernel_end - __kernel_start;

    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
```

```nasm
; kernel/arch/x86_64/entry.asm — phần _start (header ở mục 4.3)
BITS 64
extern kernel_main
extern __bss_start
extern __bss_end

section .text
global _start
_start:
    cli                               ; không tin trạng thái IF bootloader để lại (bài học P0)
    cld
    mov r12, rdi                      ; giữ BootInfo* — `rep stosb` ngay dưới dùng RDI
    lea rdi, [rel __bss_start]
    lea rcx, [rel __bss_end]
    sub rcx, rdi
    xor eax, eax
    rep stosb                         ; zero .bss (gồm cả boot_stack dưới đây — chưa dùng tới nên an toàn)
    lea rsp, [rel boot_stack_top]
    xor ebp, ebp                      ; đáy chuỗi frame cho backtrace
    mov rdi, r12
    call kernel_main                  ; [[noreturn]]
.hang:
    cli
    hlt
    jmp .hang

section .bss
align 16
boot_stack_bottom:
    resb 16384
boot_stack_top:
```

### 6.3 `tools/mkimage.py` — ghép đĩa có kiểm tra

Thay `cp + truncate + cat` trong Makefile hiện tại (lỗi P1-b: `truncate` cắt
cụt kernel quá cỡ mà không báo).

```python
#!/usr/bin/env python3
"""Ghép os.img = Stage1+Stage2 | DiskManifest | kernel.bin | initrd.tar (docs/architecture.md mục 4.1)."""
import struct
import sys

SECTOR = 512
STAGE2_SECTORS = 32                    # PHẢI khớp SECTORS_TO_LOAD trong boot/boot.asm
MANIFEST_LBA = 1 + STAGE2_SECTORS
KERNEL_LBA = MANIFEST_LBA + 1
BOOT_MAPPED_LIMIT = 1 << 30            # Stage2 chỉ map 1GB đầu (5.1.3)


def sectors(n: int) -> int:
    return (n + SECTOR - 1) // SECTOR


def pad(data: bytes) -> bytes:
    return data + b"\0" * (sectors(len(data)) * SECTOR - len(data))


def main(boot_path: str, kernel_path: str, initrd_path: str, out_path: str) -> None:
    boot = open(boot_path, "rb").read()
    kernel = open(kernel_path, "rb").read()
    initrd = open(initrd_path, "rb").read() if initrd_path != "-" else b""

    if len(boot) != MANIFEST_LBA * SECTOR:
        sys.exit(f"boot.bin phai dung {MANIFEST_LBA * SECTOR} byte, dang la {len(boot)}")

    magic, entry, load_phys, file_size, mem_size = struct.unpack_from("<5Q", kernel, 0)
    if magic != int.from_bytes(b"MYOSKRNL", "little"):
        sys.exit("kernel.bin khong bat dau bang kernel_image_header (linker.ld co KEEP(.boot_header) chua?)")
    if file_size != len(kernel):
        sys.exit(f"header noi file_size={file_size} nhung kernel.bin dai {len(kernel)} byte")
    initrd_phys = (load_phys + mem_size + 0xFFF) & ~0xFFF
    if initrd_phys + len(initrd) > BOOT_MAPPED_LIMIT:
        sys.exit("kernel + initrd vuot 1GB ma bootloader map san")

    kernel_sectors = sectors(len(kernel))
    initrd_sectors = sectors(len(initrd))
    manifest = struct.pack("<QIIIIII", int.from_bytes(b"MYOSDISK", "little"), 1,
                           KERNEL_LBA, kernel_sectors,
                           KERNEL_LBA + kernel_sectors, initrd_sectors, 0)
    with open(out_path, "wb") as out:
        out.write(boot)
        out.write(manifest.ljust(SECTOR, b"\0"))
        out.write(pad(kernel))
        out.write(pad(initrd))
    print(f"{out_path}: kernel {kernel_sectors} sector (mem {mem_size} B, entry {entry:#x}), "
          f"initrd {initrd_sectors} sector @ {initrd_phys:#x}")


if __name__ == "__main__":
    if len(sys.argv) != 5:
        sys.exit("dung: mkimage.py boot.bin kernel.bin initrd.tar|- os.img")
    main(*sys.argv[1:5])
```

### 6.4 Target Makefile & QEMU

```make
QEMU_BASE = qemu-system-x86_64 -m 256M -drive format=raw,file=os.img -serial stdio -no-reboot

run:   os.img ; $(QEMU_BASE)
debug: os.img ; $(QEMU_BASE) -d int,cpu_reset -no-shutdown          # nghi triple fault / ngắt lạ
gdb:   os.img ; $(QEMU_BASE) -s -S                                   # chờ GDB ở :1234
test:  ; $(MAKE) KTEST=1 os.img && \
         $(QEMU_BASE) -display none -device isa-debug-exit,iobase=0xf4,iosize=0x04 ; \
         test $$? -eq 33                                              # (0x10 << 1) | 1 = 33 = pass
disk:  ; mkfs.fat -F 32 -C build/disk.img 65536 && mcopy -i build/disk.img build/bin/* ::/   # M7
run-disk: os.img disk ; $(QEMU_BASE) -drive format=raw,file=build/disk.img,index=1,media=disk  # primary slave
```

**Debug bằng GDB:**

```bash
make gdb                                   # terminal 1
gdb kernel/kernel.elf \
    -ex "target remote :1234" \
    -ex "hbreak kernel_main" \
    -ex "continue"                         # terminal 2
# hbreak (hardware) thay vì break: địa chỉ higher-half chưa được map lúc QEMU mới khởi động,
# GDB không chèn được breakpoint phần mềm (ghi int3) vào đó.
# Debug Stage1/Stage2:  set architecture i8086  →  break *0x7c00
# Giải mã địa chỉ trong backtrace:  addr2line -f -C -e kernel/kernel.elf 0xffffffff80101234
```

### 6.5 Bảng triệu chứng → nguyên nhân thường gặp

| Triệu chứng | Nguyên nhân hay gặp nhất | Cách xác nhận |
|---|---|---|
| QEMU tự reset liên tục | Triple fault | `make debug`: tìm `check_exception old: 0x8 new 0x...` |
| "Double Fault" với `RIP = 0x8` | Ngắt phần cứng bật khi PIC chưa remap (lỗi P0) | Log `-d int` có `Servicing hardware INT=0x08` |
| #GP ngay sau `iretq`/`retfq` | Selector sai hoặc DPL entry GDT sai | Error code của #GP = selector gây lỗi |
| #PF error code có bit 3 (`0x8`) | PTE có bit reserved: dùng bit NX mà chưa bật `EFER.NXE`, hoặc PTE rác | `info tlb` / `info mem` trong QEMU monitor |
| #UD trong `memcpy` hoặc khi copy struct | g++ sinh SSE (thiếu `-mgeneral-regs-only`) | `objdump -d kernel.elf \| grep xmm` |
| `undefined reference to __stack_chk_fail` | Distro bật stack protector | thêm `-fno-stack-protector` |
| `undefined reference to memcpy` | Chưa có `string.asm` | — |
| #PF ở `0xb8xxx` ngay sau `vmm_init` | Quên `phys_to_virt()` cho VGA / thiết bị | CR2 trong panic |
| Hỏng dữ liệu ngẫu nhiên khi có ngắt | Thứ tự push ≠ `InterruptFrame`, thiếu `cld`, hoặc thiếu `-mno-red-zone` | ktest: `int3` với giá trị register biết trước |
| #GP lúc `sysret` | `STAR` sai, hoặc RCX không canonical | `info registers` tại #GP |
| Treo không có output | Treo trước `serial_init`, hoặc thiếu `-serial stdio` | GDB `hbreak _start`, bước từng lệnh |
| Deadlock / `KASSERT SpinLock da bi giu` | Lấy lại khoá đang giữ, hoặc sai thứ tự khoá (4.8) | backtrace trong panic |

---

## 7. Lộ trình M0 → M9

```
M0a Sửa lỗi an toàn ──► M0b Boot protocol ──► M1 Higher-half + VMM ──► M2 Heap
                                                                         │
           ┌─────────────────────────────────────────────────────────────┘
           ▼
M3 Ngắt sống + DPC + timer ──► M4 Kernel thread + scheduler ──► M5 User mode (ring 3)
                                                                         │
           ┌─────────────────────────────────────────────────────────────┘
           ▼
M6 Console tương tác + shell ──► M7 Lưu trữ (ATA, FAT32) ──► M8 libc + shell thật ──► M9+ mở rộng
```

**Quy ước "xong" cho mọi mốc:** (1) build sạch ở `-O2` và `-O0`; (2) `make test`
pass; (3) thấy đúng kết quả kiểm chứng trên QEMU; (4) cập nhật bảng trạng thái
CLAUDE.md mục 2 và bảng ràng buộc mục 8.

### M0a — Sửa lỗi an toàn (nhỏ, làm ngay)

| Việc | File |
|---|---|
| `cli` trước `lgdt` trong Stage2 | `boot/boot.asm` |
| `cli` + `cld` đầu `_start` | `kernel/kernel_entry.asm` |
| Serial COM1 + `kprintf` + `panic` + `KASSERT` + backtrace; gom 3 bản `to_hex`/`vga_print` trùng lặp | `drivers/serial.*`, `lib/kprintf.cpp`, `lib/panic.cpp` |
| `Registers` → `InterruptFrame` có `rsp`/`ss`; sửa comment sai | `idt.hpp`, `isr_stubs.asm`, `interrupt_handlers.cpp` |
| Vector 21 → `ISR_ERR`; thêm `cld` trong `isr_common` | `isr_stubs.asm` |
| IDT đủ 256 vector + stub mặc định | `isr_stubs.asm`, `idt.cpp` |
| Remap PIC + mask toàn bộ | `arch/x86_64/pic.*` |
| IST2 cho NMI, IST3 cho #MC | `tss.cpp`, `idt.cpp` |
| GDT layout 4.6 | `gdt.*` |
| Per-CPU `Cpu` + `GS_BASE` | `arch/x86_64/cpu.*` |
| Bộ cờ 6.1, `string.asm`, `cxx_runtime.cpp`, `.init_array` trong linker | `kernel/Makefile`, `lib/`, `linker.ld` |
| Khung `ktest` + `make test` | `lib/ktest.hpp`, `Makefile` |

**Kiểm chứng:**
- `make debug` **không còn** `Servicing hardware INT=0x08`.
- `int3` cố ý → log serial in đủ 22 thanh ghi, `RSP`/`SS` hợp lý (SS = 0x10).
- Build `-O2`: `objdump -d kernel.elf | grep -c xmm` = 0.
- `make test` thoát mã 33.

**Bài học Windows:** trap frame (`KTRAP_FRAME`) — so sánh các field với `InterruptFrame`.

### M0b — Boot protocol

| Việc | File |
|---|---|
| `DiskManifest`, `BootInfo`, kernel image header | `include/kernel/bootinfo.hpp`, `boot/bootinfo.inc`, `entry.asm` |
| `tools/mkimage.py` thay `truncate` | `tools/`, `Makefile` |
| Stage2: đọc manifest, unreal mode, nạp theo khối 64 sector, E820 → `0x21000`, BootInfo → `0x20000` | `boot/boot.asm` |
| `kernel_main(BootInfo*)`, PMM đọc từ BootInfo | `kernel_main.cpp`, `mm/pmm.*` |

**Kiểm chứng:**
- Thêm mảng `const` 300KB có checksum biết trước vào kernel → boot → `kprintf` checksum khớp.
- initrd giả 100KB: in 16 byte đầu, khớp `xxd`.

**Bài học Windows:** `LOADER_PARAMETER_BLOCK` — winload truyền gì cho ntoskrnl, vì sao.

### M1 — Higher-half + VMM

| Việc | File |
|---|---|
| Linker VMA cao / LMA thấp (6.2); Stage2 map 3 view (5.1.3); jump qua header | `linker.ld`, `boot.asm` |
| `ke/irql.cpp` + `SpinLock` (chưa có DPC/scheduler: 2 cờ luôn false) | `ke/` |
| `paging.hpp`, `pt_walk`, `vmm_map_page`/`unmap` | `arch/x86_64/paging.hpp`, `mm/vmm.cpp` |
| `vmm_init`: PDPT kernel cấp sẵn, HHDM 2MB, W^X, `EFER.NXE`, `CR0.WP`, bỏ identity | `mm/vmm.cpp` |
| `pmm_alloc_frame_below`, khoá PMM | `mm/pmm.*` |
| Mọi truy cập vật lý đi qua `phys_to_virt` (VGA!) | toàn bộ |

**Kiểm chứng (mỗi dòng một ktest hoặc demo):**
- Deref `nullptr` → panic "#PF addr=0x0".
- Ghi vào `__text_start` → #PF có bit W (nhờ `CR0.WP`).
- Nhảy vào một byte trong `.data` → #PF có bit I/D (nhờ NX).
- Chạy `-m 4G`: ghi/đọc một frame > 2GB qua `phys_to_virt` thành công.
- Sau `vmm_init`, đọc địa chỉ `0x100000` (identity cũ) → #PF.

**Bài học Windows:** page table 4 cấp, PTE bit, `MiGetPteAddress`. Bài tập
phụ: thêm recursive map PML4[510] rồi tự viết `get_pte_address(va)` theo đúng
công thức Windows.

### M2 — Kernel heap

| Việc | File |
|---|---|
| `kmalloc`/`kfree` có tag (5.5.4), `operator new/delete` | `mm/heap.*`, `mm/heap_cxx.cpp` |
| `kstack_alloc` có guard page | `mm/kstack.cpp` |
| Lệnh debug `heap_dump_by_tag()` | `mm/heap.cpp` |

**Kiểm chứng:**
- ktest: 10.000 lần alloc/free kích thước ngẫu nhiên (LCG có seed) → cuối cùng `free_list` gộp lại còn đúng 1 khối/vùng, `pmm_free_frames()` không giảm thêm.
- `kfree` hai lần → KASSERT.
- Đệ quy vô hạn trên stack từ `kstack_alloc` → #DF trên IST1, panic in RSP nằm trong guard page.

**Bài học Windows:** pool, pool tag, công cụ poolmon, vì sao Driver Verifier bắt được double free.

### M3 — Ngắt phần cứng sống + DPC + timer

| Việc | File |
|---|---|
| `interrupt_dispatch`, `irq_register`, spurious IRQ | `arch/x86_64/interrupts.cpp`, `irq.cpp`, `pic.cpp` |
| DPC queue | `ke/dpc.*` |
| PIT 100Hz, `ticks`, `timer_on_tick` | `arch/x86_64/pit.*`, `ke/timer.*` |
| `ke_lower_irql` bản đầy đủ; `kernel_main` hạ về PASSIVE | `ke/irql.cpp` |
| Keyboard ISR + DPC, tạm in ký tự bằng `kprintf` | `drivers/keyboard.*` |

**Kiểm chứng:**
- In `ticks` mỗi giây: tăng đều ~100/s.
- Gõ phím → ký tự hiện ra; giữ phím 10 giây → không mất phím, không panic.
- `KASSERT` trong `kmalloc` rằng IRQL ≤ DISPATCH → gọi thử `kmalloc` trong ISR phải panic.

**Bài học Windows:** IRQL, ISR/DPC split, vì sao `KeWaitForSingleObject` bị cấm ở DISPATCH_LEVEL.

### M4 — Kernel thread + scheduler preemptive

| Việc | File |
|---|---|
| `Thread`, `switch_context`, `thread_trampoline` | `ke/thread.*`, `arch/x86_64/context_switch.asm` |
| Ready queue, `sched_schedule_locked`, quantum, idle, reaper | `ke/sched.*` |
| `ke_sleep_ms`, `KEvent`, `KMutex` | `ke/timer.cpp`, `ke/wait.*` |
| Thêm `KASSERT(irql == DISPATCH && !(flags & IF))` đầu `sched_schedule_locked` | `ke/sched.cpp` |

**Kiểm chứng:**
- 2 thread in `A`/`B` không `yield`: xen kẽ nhau (bằng chứng preempt).
- `ke_sleep_ms(1000)` → `ticks` chênh 100 ± 1.
- 4 thread cùng tăng 1 biến đếm 100.000 lần dưới `KMutex`, có `ke_sleep_ms(0)` xen giữa → kết quả đúng 400.000; bỏ mutex → sai (chứng minh race có thật).
- Tạo và kết thúc 1.000 thread → `pmm_free_frames()` trở lại như ban đầu (reaper hoạt động).

**Bài học Windows:** trạng thái KTHREAD, quantum, dispatcher database, wait block.

### M5 — User mode (ring 3) — điểm ngoặt kiến trúc

Chia 2 bước để phát hiện lỗi thiết kế syscall/TSS **trước khi** có VFS:

**M5a — ring 3 tối thiểu (chưa VFS):** chương trình user `hello` nhúng vào
kernel bằng `incbin`.

| Việc | File |
|---|---|
| `tss_set_rsp0` trong scheduler; `swapgs` trong `isr_common` | `ke/sched.cpp`, `isr_stubs.asm` |
| `syscall_init`, `syscall_entry`, `SyscallFrame`, bảng syscall | `ps/syscall.cpp`, `arch/x86_64/syscall_entry.asm` |
| Address space riêng, VMA, page fault demand-zero | `mm/address_space.*`, `mm/fault.cpp` |
| `copy_from_user` + exception table | `arch/x86_64/user_copy.asm`, `mm/user_copy.hpp` |
| ELF loader (đọc từ buffer trong RAM), `enter_user_mode` | `ps/elf_loader.*`, `arch/x86_64/user_entry.asm` |
| `sys_write` (thẳng ra console), `sys_exit` | `ps/`, `io/` |

**M5b — process thật:**

| Việc | File |
|---|---|
| `ob/`: object header, handle table | `ob/` |
| VFS tối thiểu + tarfs + initrd | `fs/vfs.*`, `fs/path.cpp`, `fs/tarfs.*` |
| `ps_spawn`, `sys_spawn`, `sys_wait`, `sys_close`, `/bin/init` | `ps/process.*` |

**Kiểm chứng:**
- `hello from ring 3` qua `sys_write`.
- User đọc `0xFFFFFFFF80100000` → process bị kill, kernel thread nền vẫn in bình thường.
- `write(1, (char*)0x1234, 10)` → trả `E_FAULT`, process không chết.
- `write(1, (char*)0xFFFF800000000000, 10)` → `E_FAULT` (bị `is_user_range` chặn).
- Process chạy `for(;;){}` → vẫn bị preempt, shell/thread khác vẫn chạy.
- Process dùng 8MB stack đệ quy → OK; vượt → bị kill.

**Bài học Windows:** `KiSystemCall64`, `swapgs`, `KTRAP_FRAME` của syscall, `ProbeForRead`, handle table, `ObReferenceObjectByHandle`.

### M6 — Console tương tác + shell

| Việc | File |
|---|---|
| Mô hình driver: `DriverObject`/`DeviceObject`/`Irp`/`io_call_driver` | `io/driver.*` |
| devfs, TTY (line discipline), VGA console có cuộn + con trỏ phần cứng | `io/devfs.*`, `drivers/tty.*`, `drivers/vga_console.*` |
| `sys_read`, `sys_open` | `io/sys_file.cpp` |
| libc tối thiểu, shell | `libc/`, `userland/shell/` |
| **Bài tập:** filter driver viết hoa gắn lên tty0 | `drivers/upper_filter.cpp` |

**Kiểm chứng:**
- Shell nhận lệnh, `spawn` được `/bin/hello` từ initrd, chờ nó kết thúc.
- Backspace hoạt động.
- Bật filter → mọi output thành chữ hoa, tắt filter → trở lại bình thường mà không sửa tty.

**Bài học Windows:** IRP, device stack, filter driver, `IoCallDriver`.

═══ **Tới đây là một OS "chạy được" hoàn chỉnh theo nghĩa tối thiểu** ═══

### M7 — Lưu trữ

| Việc | File |
|---|---|
| ATA PIO LBA28 (polling), `IDENTIFY` | `drivers/ata.*` |
| Block cache | `fs/block_cache.*` |
| FAT32 chỉ đọc (8.3), mount `/disk` | `fs/fat32/` |
| `make disk`, `make run-disk` | `Makefile` |

**Kiểm chứng:**
- `spawn("/disk/bin/hello")` chạy chương trình copy vào `disk.img` bằng `mcopy`.
- Đọc cùng file 2 lần: lần 2 không phát IRP xuống ATA (đếm trong block cache).

**Bài học Windows:** storage stack, Cache Manager (so sánh cache theo block với cache theo view).

### M8 — libc + shell thật

`sys_readdir`, `ls`/`cat`/`echo`; `sys_mmap` ẩn danh + `malloc` user; truyền
`argv`; FAT32 ghi (tuỳ chọn); ATA theo ngắt IRQ14 + IRP bất đồng bộ (`E_PENDING` + completion).

### M9+ — Mở rộng (chọn theo hứng thú học)

| Hướng | Nội dung | Tương ứng Windows |
|---|---|---|
| FPU/SSE | `CR4.OSFXSR`, FXSAVE/XSAVE khi switch, lazy qua #NM | Lịch sử lazy FPU, `KeSaveExtendedProcessorState` |
| APIC | LAPIC timer, IOAPIC thay PIC | HAL APIC |
| SMP | Khởi động AP, per-CPU thật, spinlock atomic, IPI TLB shootdown | KPRCB, `KeFlushSingleTb` |
| Copy-on-write + `fork` | PFN database có refcount | PFN database, prototype PTE |
| Page cache + mmap file | VMA trỏ vào file | Section object, Cache Manager |
| Priority scheduling | Priority boost khi I/O xong, chống starvation | Priority boost, balance set manager |
| Symbol trong panic | Nhúng bảng symbol đã sort | `!analyze`, symbol server |

---

## 8. Bảng ràng buộc mới

Thay bảng mục 6 của CLAUDE.md sau khi làm xong M0b. Cột cuối là *cơ chế tự
bắt lỗi* — mục tiêu là mỗi ràng buộc đều có ít nhất một cơ chế như vậy thay vì
chỉ trông vào trí nhớ.

| Ràng buộc | Nơi A | Nơi B | Tự phát hiện lệch bằng |
|---|---|---|---|
| `SECTORS_TO_LOAD = 32` | `boot/boot.asm` | `tools/mkimage.py` (`STAGE2_SECTORS`) | mkimage kiểm tra `len(boot.bin)` |
| Layout `DiskManifest` | `boot/bootinfo.inc` | `mkimage.py` (`struct.pack`), `bootinfo.hpp` | `static_assert` + Stage2 kiểm tra magic |
| Kernel image header (offset +8 entry, +32 mem_size) | `entry.asm` | `boot.asm`, `mkimage.py` | mkimage kiểm tra magic + `file_size` |
| Layout `BootInfo`, địa chỉ `0x20000` | `boot/bootinfo.inc` | `bootinfo.hpp` | `static_assert(offsetof)` + `KASSERT(magic)` |
| `KERNEL_VMA` / `KERNEL_PHYS` | `linker.ld` | `boot.asm` (PML4[511]/PDPT[510]), `types.hpp` | `KASSERT` trong `vmm_init`: `_start - KERNEL_VMA == 0x100000 + offset` |
| `HHDM_BASE` | `types.hpp` | `boot.asm` (PML4[256], `mov rdi`) | `KASSERT(boot->magic)` sai ngay nếu lệch |
| Page table khởi động `0x1000`–`0x4FFF` | `boot.asm` | PMM (khoá < 1MB) | — |
| Selector GDT (4.6) | `gdt.hpp` | `syscall_init` (STAR), `user_entry.asm` (`0x23`/`0x2B`) | `static_assert(USER_CODE_SELECTOR == (USER_BASE_SELECTOR + 16) \| 3)` |
| Offset `Cpu` (0/8/16/24) | `cpu.hpp` | `syscall_entry.asm` | `static_assert(offsetof)` |
| Thứ tự `InterruptFrame` | `idt.hpp` | `isr_stubs.asm` | `static_assert(sizeof)` + ktest `int3` với register biết trước |
| Thứ tự `SyscallFrame` | `ps/syscall.cpp` | `syscall_entry.asm` | `static_assert(sizeof)` + ktest syscall echo |
| Vector có error code (8, 10–14, 17, 21) | `isr_stubs.asm` | Intel SDM bảng 6-1 | ktest gây #GP/#PF, kiểm tra `error_code` |
| IST: 1=#DF, 2=NMI, 3=#MC | `idt.cpp` | `tss.cpp` | — |
| Vector IRQ bắt đầu từ 32 | `pic.cpp` | `interrupts.cpp` (`IRQ_BASE_VECTOR`) | dùng chung 1 hằng |
| Symbol `__ex_table_*`, `__init_array_*`, `__ktests_*`, `__text_*`… | `linker.ld` | `user_copy.hpp`, `cxx_runtime.cpp`, `ktest.hpp`, `vmm.cpp` | lỗi link nếu thiếu |
| Số hiệu syscall | `include/kernel/syscall_numbers.hpp` | `libc/` | dùng chung header |
| Bất biến IRQL: IF=0 ⇔ IRQL ≥ DEVICE; switch ở DISPATCH + IF=0 | `ke/` | `arch/x86_64/interrupts.cpp`, `syscall_entry.asm` | `KASSERT` đầu `sched_schedule_locked` |
| Thứ tự khoá: KMutex → SpinLock → IrqSpinLock. Giữa các SpinLock: `HandleTable.lock` → `heap_lock` → `AddressSpace.lock` → `pmm_lock`. **Không `kmalloc`/`new` khi đang giữ `AddressSpace.lock`** (cấp `VmArea` trước rồi mới khoá) | toàn kernel | — | `KASSERT` trong `SpinLock` bắt lấy lại cùng khoá; thứ tự khác nhau giữa các khoá thì chỉ review mới bắt được |
| Page fault chỉ hợp lệ ở IRQL < DISPATCH | `mm/fault.cpp` | mọi code giữ `SpinLock` | panic có chẩn đoán |
| User link tại `0x400000`, segment không chung trang | Makefile userland | `elf_loader.cpp` (`USER_BASE`, `vma_add` từ chối chồng lấn) | loader trả `E_NOEXEC` |
| `-mgeneral-regs-only` cho cả kernel lẫn user | 2 Makefile | context switch không lưu XMM | `objdump \| grep xmm` trong `make test` |

---

## 9. Những chỗ CLAUDE.md cần sửa

| Mục CLAUDE.md | Hiện ghi | Cần sửa thành |
|---|---|---|
| 5.4, 6 (dòng `Registers`), 8(b) | "`Registers` không có rsp/ss vì ring 0; khi có ring 3 phải thêm và sửa `isr_common` xử lý 5 trường thay vì 3" | Long Mode **luôn** push 5 trường (SDM §6.14.2). Thêm `rsp`/`ss` ngay (M0a); ring 3 **không** đổi layout frame, chỉ thêm `swapgs` + `TSS.RSP0` |
| 2 (dòng PIC), 7 (nợ #1) | "PIC chưa cần ngay, chỉ bắt buộc trước khi gọi `sti`" | IF **đã** = 1 từ Stage1 (lỗi P0, mục 1.1) → remap + mask là việc M0a |
| 5.3, 5.4 (tiêu đề) | `kernel/gdt.*`, `kernel/idt.*`… | `kernel/arch/x86_64/...` |
| 5.4 | "khi có Physical Frame Allocator, Page Fault hợp lệ phải `iretq`" | Cần VMM + VMA (M1/M5), không phải PFA |
| 6 (dòng `KERNEL_SECTORS`) | "tăng cả hai nơi là đủ" | Không đủ: >128 sector đè E820 ở `0x20000`; `INT 13h` giới hạn 127 sector/lần; `truncate` cắt cụt âm thầm. Thay bằng manifest (M0b) |
| 5.1 | Danh sách bước Stage2 | Thêm dò E820 và `cli` trước `lgdt` vào đúng thứ tự |
| 3 (cờ compiler) | 7 cờ | Bộ cờ mục 6.1 |
| 6 (`kernel_entry.o` link đầu tiên) | ràng buộc | Biến mất sau M0b (header + `KEEP(.boot_header)`) |
| 8 (lộ trình) | 7 bước, user mode sau FAT32 | Trỏ sang mục 7 file này |
| Toàn file | Chi tiết cơ chế dài | Giữ CLAUDE.md là mục lục + trạng thái + ràng buộc; chi tiết chuyển sang `docs/architecture.md` và `docs/notes/` |

---

## 10. Tài liệu tham khảo

**Đặc tả phần cứng / chuẩn (nguồn sự thật cho mọi con số trong file này):**
- Intel SDM Vol. 3A — ch.4 *Paging*; ch.5 *Protection*; ch.6 *Interrupt and Exception Handling* (§6.14 64-bit mode, bảng 6-1 error code); ch.7 *Task Management* (§7.7 TSS 64-bit).
- AMD64 Architecture Programmer's Manual Vol. 2 — `SYSCALL`/`SYSRET`, `EFER`, `STAR`/`LSTAR`/`SFMASK`.
- System V AMD64 ABI — thanh ghi tham số, callee-saved, red zone, căn stack 16 byte, DF = 0.
- Phoenix *BIOS Enhanced Disk Drive Specification* 3.0 — `INT 13h AH=42h`, giới hạn 127 block.
- ACPI Specification — `INT 15h E820`.
- Microsoft *FAT32 File System Specification* (fatgen103).
- *ELF-64 Object File Format*; POSIX.1-1988 USTAR.

**OSDev Wiki (các trang):** Unreal Mode · Detecting Memory (x86) · Higher Half x86 Bare Bones ·
8259 PIC · Programmable Interval Timer · Serial Ports · PS/2 Keyboard · ATA PIO Mode · FAT ·
Interrupt Service Routines · SYSENTER/SYSCALL · Calling Global Constructors · Stack Smashing Protector.

**Windows (đọc song song theo mốc ở mục 7):**
- *Windows Internals* (Russinovich, Solomon, Yosifovich) — Trap dispatching (IRQL, DPC, APC, system service dispatching); Thread scheduling; Memory management (page tables, PFN database, VAD, pool); Object Manager (handle, reference counting); I/O system (IRP, driver/device object, device stack); Cache Manager.
- *Windows Kernel Programming* (Yosifovich) — Kernel mechanisms (IRQL, DPC, sync primitives); driver & device objects; filter driver.

**Linux (đối chiếu cách làm thật):** `arch/x86/entry/entry_64.S` (syscall entry,
`paranoid_entry` dùng MSR GS_BASE), `arch/x86/mm/extable.c` (exception table),
`arch/x86/mm/fault.c`.

