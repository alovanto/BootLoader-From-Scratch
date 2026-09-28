; =========================================================================
; kernel_entry.asm — điểm vào thật sự của kernel (chạy ở 64-bit Long Mode)
;
; Bootloader (boot.asm) jmp thẳng vào symbol `_start` này sau khi đã bật xong
; Long Mode. Nhiệm vụ: đưa CPU về trạng thái xác định, xoá .bss, dựng stack
; riêng cho kernel (tách khỏi stack tạm 0x90000 của stage2), rồi gọi kernel_main.
; =========================================================================
BITS 64

extern kernel_main   ; hàm C++ trong kernel_main.cpp (khai báo extern "C")
extern __bss_start    ; từ linker.ld
extern __bss_end       ; từ linker.ld

section .bss
align 16
kernel_stack_bottom:
    resb 16384        ; 16KB stack cho kernel
kernel_stack_top:

section .text
global _start
_start:
    ; ---- Đưa CPU về trạng thái xác định, không tin gì từ bootloader ----
    ; `cli`: bootloader đã tắt ngắt trước khi vào Protected Mode, nhưng kernel
    ; không nên phụ thuộc vào điều đó. Ngắt chỉ được bật lại sau khi IDT đã
    ; sẵn sàng VÀ PIC đã remap (PIC mặc định đè IRQ0-7 lên vector 8-15, tức là
    ; đè lên chính các vector exception của CPU).
    ; `cld`: System V ABI quy định DF = 0 khi vào hàm C. `rep stosb` ngay dưới
    ; cũng cần DF = 0 để chạy xuôi chứ không phải ngược.
    cli
    cld

    ; ---- Xoá sạch .bss TRƯỚC KHI làm bất kỳ việc gì khác ----
    ; Trước đây .bss (chỉ có stack kernel) tình cờ chạy đúng vì nó nhỏ và
    ; nằm lọt trong vùng đệm zero mà top-level Makefile tự thêm khi pad
    ; kernel.bin — đó là may mắn, không phải cơ chế đáng tin cậy. Từ khi
    ; Physical Frame Allocator thêm bitmap 128KB vào .bss, không thể dựa vào
    ; may mắn đó nữa: bitmap phải THẬT SỰ bắt đầu từ toàn số 0.
    lea rdi, [rel __bss_start]
    lea rcx, [rel __bss_end]
    sub rcx, rdi
    xor eax, eax
    rep stosb

    mov rsp, kernel_stack_top
    xor ebp, ebp             ; RBP = 0 đánh dấu đáy chuỗi khung ngăn xếp, để
                             ; backtrace() trong panic biết chỗ dừng
    call kernel_main

    ; Nếu kernel_main lỡ return (không nên xảy ra), dừng máy có kiểm soát
    ; thay vì để CPU chạy lung tung vào vùng nhớ rác.
    cli
.hang:
    hlt
    jmp .hang
