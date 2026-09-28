; =========================================================================
; isr_stubs.asm — stub cho toàn bộ 256 vector của IDT
;
; Vai trò: chuẩn hoá stack frame (một số exception CPU tự push error code,
; số khác thì không) rồi nhảy vào isr_common để lưu thanh ghi và gọi C++.
;
; Vì sao phải có đủ 256 chứ không chỉ 32 exception: bất kỳ vector nào không
; được đăng ký trong IDT mà bị kích hoạt sẽ gây #GP với error code khó hiểu,
; thay vì một dòng log nói rõ "vector N không mong đợi".
; =========================================================================
BITS 64

extern interrupt_dispatch   ; hàm C++ trong interrupt_handlers.cpp

section .text

; Exception KHÔNG có error code thật → tự push 0 giả để đồng nhất layout
%macro ISR_NOERR 1
global isr_stub_%1
isr_stub_%1:
    push qword 0        ; error code giả
    push qword %1        ; số hiệu vector
    jmp isr_common
%endmacro

; Exception CÓ error code thật (CPU đã tự push trước khi nhảy vào đây)
%macro ISR_ERR 1
global isr_stub_%1
isr_stub_%1:
    push qword %1        ; chỉ cần thêm số hiệu vector
    jmp isr_common
%endmacro

; Danh sách 32 exception chuẩn của x86_64 (Intel SDM Vol.3, bảng 6-1).
; Các vector CÓ error code thật: 8, 10, 11, 12, 13, 14, 17, 21
ISR_NOERR 0    ; #DE Divide Error
ISR_NOERR 1    ; #DB Debug
ISR_NOERR 2    ; NMI            (chạy trên IST2)
ISR_NOERR 3    ; #BP Breakpoint
ISR_NOERR 4    ; #OF Overflow
ISR_NOERR 5    ; #BR BOUND Range Exceeded
ISR_NOERR 6    ; #UD Invalid Opcode
ISR_NOERR 7    ; #NM Device Not Available
ISR_ERR   8    ; #DF Double Fault (chạy trên IST1)
ISR_NOERR 9    ; Coprocessor Segment Overrun (legacy)
ISR_ERR   10   ; #TS Invalid TSS
ISR_ERR   11   ; #NP Segment Not Present
ISR_ERR   12   ; #SS Stack-Segment Fault
ISR_ERR   13   ; #GP General Protection Fault
ISR_ERR   14   ; #PF Page Fault
ISR_NOERR 15   ; reserved
ISR_NOERR 16   ; #MF x87 FPU Error
ISR_ERR   17   ; #AC Alignment Check
ISR_NOERR 18   ; #MC Machine Check (chạy trên IST3)
ISR_NOERR 19   ; #XM SIMD FP Exception
ISR_NOERR 20   ; #VE Virtualization Exception
ISR_ERR   21   ; #CP Control Protection — CÓ error code (bảng 6-1 Intel SDM)
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30   ; #SX Security Exception (chỉ AMD; có error code khi chạy trên AMD thật)
ISR_NOERR 31

; Vector 32-255: IRQ phần cứng (sau khi remap PIC) và phần chưa dùng.
; Không vector nào trong dải này có error code.
%assign vector_index 32
%rep 224
global isr_stub_%+vector_index
isr_stub_%+vector_index:
    push qword 0
    push qword vector_index
    jmp isr_common
%assign vector_index vector_index+1
%endrep

; -------------------------------------------------------------------------
; isr_common — điểm hội tụ của cả 256 stub
;
; Stack khi vào đây (từ địa chỉ thấp lên cao):
;   [rsp]    vector        (stub push)
;   [rsp+8]  error code    (CPU hoặc stub push)
;   [rsp+16] RIP           ┐
;   [rsp+24] CS            │ CPU push — ở Long Mode LUÔN đủ 5 ô,
;   [rsp+32] RFLAGS        │ kể cả ring 0 → ring 0
;   [rsp+40] RSP           │
;   [rsp+48] SS            ┘
; -------------------------------------------------------------------------
isr_common:
    ; Lưu toàn bộ general-purpose register. THỨ TỰ NÀY PHẢI KHỚP CHÍNH XÁC
    ; với struct InterruptFrame trong idt.hpp (đọc từ dưới lên = từ địa chỉ
    ; thấp lên cao, vì push sau nằm ở địa chỉ thấp hơn push trước).
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

    ; System V ABI yêu cầu DF = 0 khi vào hàm C. Ngắt là bất đồng bộ nên có thể
    ; xảy ra đúng lúc một hàm đang tạm đặt DF = 1 (memmove copy ngược) — không
    ; `cld` ở đây thì hàm C++ bên dưới sẽ copy ngược mà không ai ngờ tới.
    cld

    ; Tham số đầu tiên truyền qua RDI. RSP hiện tại chính là con trỏ tới
    ; struct InterruptFrame hoàn chỉnh.
    ;
    ; Căn stack: CPU căn RSP về bội 16 trước khi push 5 ô (40 byte), stub thêm
    ; 16 byte (hoặc CPU 48 + stub 8), rồi 15 lần push = 120 byte
    ; → tổng 176 byte = 11 × 16, nên RSP chia hết cho 16 trước `call`, đúng ABI.
    mov rdi, rsp
    call interrupt_dispatch

    ; Khôi phục đúng thứ tự ngược lại
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

    add rsp, 16          ; bỏ 2 field [vector, error_code] ta đã tự thêm vào
    iretq                ; CPU tự pop RIP, CS, RFLAGS, RSP, SS

    ; GHI CHÚ cho bước user mode (ring 3): khi đó cần thêm `swapgs` ở đầu và
    ; cuối isr_common, có điều kiện theo bit RPL của CS đã push
    ; (xem docs/architecture.md mục 5.2.3). Chưa thêm bây giờ vì kernel chạy
    ; 100% ring 0 — thêm sớm chỉ tạo code chết không kiểm chứng được.

; -------------------------------------------------------------------------
; Bảng con trỏ 256 stub, để idt.cpp duyệt qua và đăng ký vào IDT
; -------------------------------------------------------------------------
section .data
global isr_stub_table
isr_stub_table:
%assign table_index 0
%rep 256
    dq isr_stub_%+table_index
%assign table_index table_index+1
%endrep
