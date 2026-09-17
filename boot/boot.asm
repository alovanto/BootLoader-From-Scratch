; =========================================================================
; boot.asm
; Một bootloader tối giản minh hoạ đầy đủ chuỗi:
;   BIOS -> Real Mode (16-bit) -> Protected Mode (32-bit) -> Long Mode (64-bit)
;
; File này build ra MỘT ảnh đĩa (os.img) duy nhất:
;   - 512 byte đầu   = MBR (boot sector), BIOS load vào 0x7C00 và nhảy vào đây
;   - Các sector sau = "Stage 2" (được Stage 1 tự đọc từ đĩa lên RAM)
;
; Build: nasm -f bin -o os.img boot.asm
; Chạy:  qemu-system-x86_64 -drive format=raw,file=os.img
; =========================================================================

BITS 16
ORG 0x7C00     ;báo cho NASM: "khi tính các label/địa chỉ trong file này, giả định code đang nằm ở RAM bắt đầu từ 0x7C00". 
               ;Nếu không có dòng này, các phép nhảy jmp bên trong sẽ tính sai địa chỉ.



KERNEL_LOAD_SEG   equ 0x0000
KERNEL_LOAD_OFF   equ 0x7E00      ; nạp Stage2 ngay sau boot sector
SECTORS_TO_LOAD   equ 32          ; 32*512 = 16KB cho stage2 (dư dả cho demo)

; ---- Nạp kernel.bin thật (build riêng trong thư mục kernel/) ----
; Real mode chỉ addressing thoải mái dưới 1MB, nhưng linker.ld đặt kernel
; ở đúng 1MB — nên phải nạp tạm xuống dưới rồi copy lên sau khi vào
; Protected Mode (xem đoạn `rep movsd` ở protected_mode_entry).
KERNEL_BIN_SEG    equ 0x1000                    ; => physical 0x10000, không đụng Stage2
KERNEL_BIN_OFF    equ 0x0000
KERNEL_SECTORS    equ 128                       ; 128*512 = 64KB — dư dả cho vài phiên bản kernel
                                                 ; tới trước khi cần tăng con số này lên
KERNEL_BIN_LBA    equ (1 + SECTORS_TO_LOAD)     ; nằm ngay sau Stage2 trên đĩa
KERNEL_PHYS_ADDR  equ 0x100000                  ; PHẢI khớp `. = 0x100000;` trong linker.ld

; ---- Bản đồ RAM thật (E820), dò bằng BIOS INT 15h ở Real Mode ----
; Đặt ngay sau vùng tạm kernel.bin (0x10000 - 0x20000) để không đụng nhau.
; PHẢI khớp `E820_MAP_PHYS_ADDR` trong kernel/mm/e820.hpp — đây là hợp đồng
; boot protocol giữa bootloader và kernel, đổi một bên phải đổi bên kia.
E820_SEG          equ 0x2000                    ; => physical 0x20000
E820_OFF          equ 0x0000
E820_MAX_ENTRIES  equ 32                         ; QEMU thường trả 6-10 entry, 32 là dư dả

; -------------------------------------------------------------------------
; STAGE 1 — chạy ở Real Mode, nhiệm vụ duy nhất: đọc Stage2 từ đĩa rồi nhảy tới
; -------------------------------------------------------------------------
start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00          ; stack ngay dưới nơi boot sector được nạp
    sti

    mov [boot_drive], dl    ; BIOS truyền ổ đĩa boot vào DL, lưu lại để dùng sau

    mov si, msg_stage1
    call print16

    ; Đọc Stage2 bằng INT 13h AH=42h (LBA extension) — an toàn hơn CHS
    ; vì không phụ thuộc vào geometry của ổ đĩa.
    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

    ; Nhảy sang Stage2 vừa nạp
    jmp KERNEL_LOAD_SEG:KERNEL_LOAD_OFF

disk_error:
    mov si, msg_disk_err
    call print16
.hang:
    hlt
    jmp .hang

; In chuỗi ký tự kết thúc bằng 0 ra màn hình bằng BIOS interrupt (chỉ dùng được ở Real Mode)
print16:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp print16
.done:
    ret

boot_drive:   db 0
msg_stage1:   db "Stage1: doc Stage2 tu dia...", 13, 10, 0
msg_disk_err: db "Loi doc dia!", 13, 10, 0

; Disk Address Packet cho INT 13h AH=42h (Extended Read)
dap:
    db 0x10                 ; kich thuoc packet
    db 0                    ; reserved
    dw SECTORS_TO_LOAD       ; so sector can doc
    dw KERNEL_LOAD_OFF       ; offset dich
    dw KERNEL_LOAD_SEG       ; segment dich
    dq 1                     ; LBA bat dau = sector 1 (ngay sau MBR)

; Đệm cho đủ 512 byte và đặt boot signature — bắt buộc để BIOS nhận đây là ổ đĩa bootable
times 510-($-$$) db 0
dw 0xAA55

; =========================================================================
; STAGE 2 — được nạp tại vật lý 0x7E00, vẫn chạy Real Mode lúc đầu
; =========================================================================
stage2_start:
    mov si, msg_stage2
    call print16b

    ; ---- Nạp kernel.bin (đã build sẵn, nằm ngay sau Stage2 trên đĩa) ----
    ; Nạp tạm vào 0x10000 (dưới 1MB, real mode addressing tới được).
    mov si, msg_kernel
    call print16b

    mov si, dap_kernel
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

    ; ---- Dò bản đồ RAM thật qua BIOS INT 15h, EAX=0xE820 ----
    ; BẮT BUỘC làm ở đây: dịch vụ BIOS này chỉ gọi được ở Real Mode, một khi
    ; đã `mov cr0` bật Protected Mode phía dưới thì không còn cách nào hỏi
    ; lại BIOS RAM thật nằm ở đâu nữa (đây là lý do trước đây bootloader phải
    ; identity-map "mù" cứng 1GB thay vì map đúng theo RAM thật).
    mov si, msg_e820
    call print16b
    call detect_memory_e820

    ; ---- Bật đường A20 (fast A20 gate qua port 0x92) ----
    ; Nếu không bật A20, CPU sẽ "wrap around" bộ nhớ ở 1MB như thời 8086,
    ; khiến việc truy cập bộ nhớ trên 1MB (bắt buộc cho protected mode) bị lỗi.
    in al, 0x92
    or al, 2
    out 0x92, al

    ; ---- Nạp GDT — bắt buộc phải có trước khi vào Protected Mode ----
    lgdt [gdt_descriptor]

    ; ---- Bật Protected Mode: set bit PE (bit 0) trong CR0 ----
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; Far jump vào code segment 32-bit — bắt buộc để CPU nạp lại CS
    ; và xả prefetch queue (đang chứa lệnh 16-bit dở dang)
    jmp CODE_SEG32:protected_mode_entry

print16b:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp print16b
.done:
    ret

; -------------------------------------------------------------------------
; detect_memory_e820 — dò bản đồ RAM thật, ghi kết quả vào E820_SEG:0000
;
; Layout buffer ghi ra (đọc lại từ kernel qua kernel/mm/e820.hpp):
;   offset 0  : word  entry_count (word thấp; word cao tại offset 2 luôn 0)
;   offset 4+ : mảng E820Entry 20-byte liên tiếp (base:8, length:8, type:4)
;
; Giao thức gọi BIOS INT 15h AX=0xE820 (Intel/ACPI, không phải tự đặt):
;   EAX=0xE820, EDX='SMAP', ECX=kich thuoc buffer entry, ES:DI=buffer đích,
;   EBX=continuation (0 lần đầu). BIOS trả EAX='SMAP' nếu thành công, EBX=0
;   nếu đây là entry cuối cùng, CF=1 nếu lỗi/không hỗ trợ.
; -------------------------------------------------------------------------
detect_memory_e820:
    push es
    push di
    push bp

    mov ax, E820_SEG
    mov es, ax
    mov word [es:0], 0      ; entry_count = 0 trước, phòng khi BIOS không trả entry nào
    mov word [es:2], 0

    mov di, 4                ; entry đầu tiên bắt đầu ngay sau 4 byte entry_count
    xor ebx, ebx              ; continuation = 0 -> bắt đầu từ đầu
    xor bp, bp                 ; bp = số entry đã ghi được

.loop:
    cmp bp, E820_MAX_ENTRIES
    jae .done                  ; hết chỗ buffer -> dừng, dùng số entry đã có

    mov eax, 0xE820
    mov ecx, 20                 ; yêu cầu entry 20-byte (base+length+type) — đủ cho PFA
    mov edx, 0x534D4150          ; chữ ký bắt buộc 'SMAP', BIOS có thể đòi hỏi mỗi lần gọi
    int 0x15
    jc .done                     ; CF=1: lỗi hoặc BIOS không hỗ trợ E820

    cmp eax, 0x534D4150           ; gọi thành công thì BIOS trả lại đúng 'SMAP' trong EAX
    jne .done

    cmp ecx, 20
    jb .skip                      ; entry < 20 byte là dị dạng, bỏ qua, vẫn thử tiếp

    add di, 20
    inc bp

.skip:
    test ebx, ebx
    je .done                      ; EBX=0 sau lệnh gọi = đây vừa là entry cuối cùng
    jmp .loop

.done:
    mov [es:0], bp                 ; ghi tổng số entry thật vào đầu buffer

    pop bp
    pop di
    pop es
    ret

msg_e820:   db "Stage2: da do xong E820 memory map.", 13, 10, 0
msg_stage2: db "Stage2: chuyen sang Protected Mode...", 13, 10, 0
msg_kernel: db "Stage2: dang nap kernel.bin tu dia...", 13, 10, 0

; Disk Address Packet thứ hai — dùng lại đúng cơ chế LBA extended read
; như dap ở Stage1, chỉ khác điểm bắt đầu và điểm đích.
dap_kernel:
    db 0x10
    db 0
    dw KERNEL_SECTORS
    dw KERNEL_BIN_OFF
    dw KERNEL_BIN_SEG
    dq KERNEL_BIN_LBA

; ---------------- Global Descriptor Table ----------------
; GDT mô tả các "segment" mà CPU dùng ở Protected/Long Mode.
; Ở đây dùng mô hình "flat" (segment bao trọn 4GB, base = 0) vì ta sẽ
; quản lý bộ nhớ hoàn toàn bằng paging, không dùng segmentation nữa.
gdt_start:
gdt_null:      dq 0                                  ; entry 0 luôn phải rỗng

gdt_code32:                                          ; code segment 32-bit
    dw 0xFFFF, 0x0000
    db 0x00, 10011010b, 11001111b, 0x00

gdt_data32:                                          ; data segment 32-bit
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 11001111b, 0x00

gdt_code64:                                          ; code segment 64-bit
    dw 0x0000, 0x0000
    db 0x00, 10011010b, 00100000b, 0x00              ; bit L=1 (long mode) trong access byte
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

CODE_SEG32 equ gdt_code32 - gdt_start
DATA_SEG32 equ gdt_data32 - gdt_start
CODE_SEG64 equ gdt_code64 - gdt_start

; =========================================================================
; 32-bit Protected Mode
; =========================================================================
BITS 32
protected_mode_entry:
    ; Nạp lại các segment register bằng selector data 32-bit
    mov ax, DATA_SEG32
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000        ; stack mới, vùng RAM thấp còn trống

    mov esi, msg_pm
    call print32

    ; ---- Copy kernel.bin từ vùng tạm 0x10000 lên đúng KERNEL_PHYS_ADDR ----
    ; Lý do phải chờ tới đây mới copy được: real mode (16-bit) không thể
    ; addressing thẳng qua 1MB bằng segment:offset thông thường, nhưng ở
    ; Protected Mode với segment flat 4GB (đã nạp DS/ES ở trên), `mov`/
    ; `rep movsd` addressing thoải mái toàn bộ RAM — không cần paging,
    ; đây vẫn là truy cập vật lý trực tiếp.
    mov esi, 0x10000
    mov edi, KERNEL_PHYS_ADDR
    mov ecx, (KERNEL_SECTORS * 512) / 4
    rep movsd

    call setup_page_tables
    call enter_long_mode

; In thẳng ra VGA text buffer (0xB8000) — không còn BIOS interrupt ở Protected Mode
print32:
    push eax
    push ebx
    mov ebx, 0xB8000
.loop:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0F             ; thuộc tính: chữ trắng nền đen
    mov [ebx], ax
    add ebx, 2
    jmp .loop
.done:
    pop ebx
    pop eax
    ret

msg_pm: db "Stage2: Protected Mode OK. Dang thiet lap Long Mode...", 0

; ---------------- Page tables cho Long Mode ----------------
; Long Mode BẮT BUỘC phải bật paging. Ở đây ta dựng bảng trang đơn giản
; nhất có thể: identity-map (địa chỉ ảo = địa chỉ vật lý) 1GB đầu tiên,
; dùng page 2MB để khỏi phải dựng cấp Page Table thứ 4 (PT).
;
; Cấu trúc 4 cấp của x86_64: PML4 -> PDPT -> PD -> (PT) -> Physical Page
PML4_ADDR equ 0x1000
PDPT_ADDR equ 0x2000
PD_ADDR   equ 0x3000

setup_page_tables:
    ; Xoá 3 trang (PML4, PDPT, PD) = 0x3000 byte bắt đầu từ 0x1000
    mov edi, PML4_ADDR
    xor eax, eax
    mov ecx, 0x3000/4
    rep stosd

    ; PML4 entry 0 trỏ tới PDPT
    mov edi, PML4_ADDR
    mov eax, PDPT_ADDR
    or eax, 0b11              ; flags: Present + Writable
    mov [edi], eax

    ; PDPT entry 0 trỏ tới PD
    mov edi, PDPT_ADDR
    mov eax, PD_ADDR
    or eax, 0b11
    mov [edi], eax

    ; Lấp đầy 512 entry của PD, mỗi entry ánh xạ 1 trang 2MB liên tiếp
    ; => tổng cộng map được 512 * 2MB = 1GB, identity mapped
    mov edi, PD_ADDR
    mov eax, 0b10000011        ; flags: Present + Writable + PageSize(2MB)
    mov ecx, 512
.fill_pd:
    mov [edi], eax
    add eax, 0x200000          ; nhảy 2MB cho trang tiếp theo
    add edi, 8                 ; mỗi entry 8 byte
    loop .fill_pd
    ret

enter_long_mode:
    ; Nạp địa chỉ PML4 vào CR3 — đây là "gốc" của toàn bộ hệ thống paging
    mov eax, PML4_ADDR
    mov cr3, eax

    ; Bật PAE (Physical Address Extension) — bit 5 của CR4
    ; Long Mode bắt buộc phải có PAE bật trước
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    ; Bật cờ LME (Long Mode Enable) trong thanh ghi MSR EFER (0xC0000080)
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    ; Bật paging — bit 31 của CR0. Ngay khi bật, CPU chính thức vào
    ; Long Mode (ở dạng "compatibility submode", vẫn đang chạy code 32-bit)
    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax

    ; Far jump vào code segment 64-bit -> chuyển hẳn sang 64-bit mode
    jmp CODE_SEG64:long_mode_entry

; =========================================================================
; 64-bit Long Mode — đây là nơi "kernel thật sự" của bạn sẽ bắt đầu
; =========================================================================
BITS 64
long_mode_entry:
    ; Ở Long Mode, segment registers gần như vô nghĩa (trừ FS/GS dùng cho
    ; TLS), nhưng vẫn cần nạp selector data hợp lệ (giá trị 0 là được)
    mov ax, 0
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov rsp, 0x90000

    ; In xác nhận trực tiếp ra VGA text buffer để biết đã vào 64-bit thật sự
    mov rdi, msg_lm
    mov rbx, 0xB8000
.loop:
    mov al, [rdi]
    cmp al, 0
    je .halt
    mov ah, 0x0A              ; chữ xanh lá sáng
    mov [rbx], ax
    add rbx, 2
    inc rdi
    jmp .loop
.halt:
    ; Bàn giao quyền điều khiển cho kernel thật (đã copy lên KERNEL_PHYS_ADDR
    ; ở bước Protected Mode phía trên). _start trong kernel_entry.asm sẽ
    ; dựng stack riêng và gọi kernel_main — bootloader không quay lại nữa.
    jmp KERNEL_PHYS_ADDR

msg_lm: db "Bootloader: ban giao dieu khien cho kernel...", 0

; Đệm cho đủ kích thước ảnh đĩa đã khai báo ở DAP (SECTORS_TO_LOAD sector)
times (512 + SECTORS_TO_LOAD*512) - ($-$$) db 0
