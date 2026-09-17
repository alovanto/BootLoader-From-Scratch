; =========================================================================
; isr_stubs.asm — stub cho 32 CPU exception (vector 0-31)
;
; Vai trò: chuẩn hoá stack frame (một số exception CPU tự push error code,
; số khác thì không) rồi nhảy vào isr_common để lưu thanh ghi và gọi C++.
; =========================================================================
BITS 64

extern isr_handler   ; hàm C++ trong interrupt_handlers.cpp

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

; Danh sách 32 exception chuẩn của x86_64 (Intel SDM Vol 3, chương Interrupts).
; Các vector CÓ error code thật: 8, 10, 11, 12, 13, 14, 17
ISR_NOERR 0    ; #DE Divide Error
ISR_NOERR 1    ; #DB Debug
ISR_NOERR 2    ; NMI
ISR_NOERR 3    ; #BP Breakpoint
ISR_NOERR 4    ; #OF Overflow
ISR_NOERR 5    ; #BR BOUND Range Exceeded
ISR_NOERR 6    ; #UD Invalid Opcode
ISR_NOERR 7    ; #NM Device Not Available
ISR_ERR   8    ; #DF Double Fault
ISR_NOERR 9    ; Coprocessor Segment Overrun (legacy)
ISR_ERR   10   ; #TS Invalid TSS
ISR_ERR   11   ; #NP Segment Not Present
ISR_ERR   12   ; #SS Stack-Segment Fault
ISR_ERR   13   ; #GP General Protection Fault
ISR_ERR   14   ; #PF Page Fault
ISR_NOERR 15   ; reserved
ISR_NOERR 16   ; #MF x87 FPU Error
ISR_ERR   17   ; #AC Alignment Check
ISR_NOERR 18   ; #MC Machine Check
ISR_NOERR 19   ; #XM SIMD FP Exception
ISR_NOERR 20   ; #VE Virtualization Exception
ISR_NOERR 21   ; #CP Control Protection (CET, hiếm gặp trên QEMU mặc định)
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

; -------------------------------------------------------------------------
; isr_common — điểm hội tụ của cả 32 stub
; -------------------------------------------------------------------------
isr_common:
    ; Lưu toàn bộ general-purpose register. THỨ TỰ NÀY PHẢI KHỚP CHÍNH XÁC
    ; với struct Registers trong idt.hpp (đọc từ dưới lên = từ địa chỉ thấp
    ; lên cao, vì push sau nằm ở địa chỉ thấp hơn push trước).
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

    ; System V AMD64 ABI: tham số đầu tiên truyền qua RDI.
    ; RSP hiện tại chính là con trỏ tới struct Registers hoàn chỉnh.
    mov rdi, rsp
    call isr_handler

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
    iretq                ; CPU tự pop RIP, CS, RFLAGS còn lại (ring0 → ring0)

; -------------------------------------------------------------------------
; Bảng con trỏ 32 stub, để idt.cpp duyệt qua và đăng ký vào IDT
; -------------------------------------------------------------------------
section .data
global isr_stub_table
isr_stub_table:
%assign i 0
%rep 32
    dq isr_stub_%+i
%assign i i+1
%endrep
