; =========================================================================
; kernel_entry.asm — điểm vào thật sự của kernel (chạy ở 64-bit Long Mode)
;
; Bootloader (boot.asm) sẽ jmp/call thẳng vào symbol `_start` này sau khi
; đã bật xong Long Mode. Nhiệm vụ duy nhất ở đây: dựng một stack riêng cho
; kernel (tách khỏi stack tạm 0x90000 mà stage2 dùng), rồi gọi kernel_main.
; =========================================================================
BITS 64

extern kernel_main   ; hàm C++ trong kernel_main.cpp (khai báo extern "C")
extern __bss_start    ; từ linker.ld
extern __bss_end       ; từ linker.ld

section .bss
align 16
kernel_stack_bottom:
    resb 16384        ; 16KB stack cho kernel — đủ cho giai đoạn IDT/ISR này
kernel_stack_top:

section .text
global _start
_start:
    ; ---- Xoá sạch .bss TRƯỚC KHI làm bất kỳ việc gì khác ----
    ; Trước đây .bss (chỉ có stack kernel) tình cờ chạy đúng vì nó nhỏ và
    ; nằm lọt trong vùng đệm zero mà top-level Makefile tự thêm khi pad
    ; kernel.bin cho đủ KERNEL_SECTORS*512 byte — đó là may mắn, không phải
    ; cơ chế đáng tin cậy. Từ khi Physical Frame Allocator (mm/pmm.cpp) thêm
    ; một bitmap 128KB vào .bss, không thể dựa vào may mắn đó nữa: bitmap
    ; phải THẬT SỰ bắt đầu từ toàn số 0 (nghĩa là "mọi frame chưa biết"),
    ; nếu không pmm_init() sẽ đọc phải rác. Xoá đúng vùng [__bss_start,
    ; __bss_end) theo 2 symbol linker.ld export, không phụ thuộc kích thước
    ; file trên đĩa.
    lea rdi, [rel __bss_start]
    lea rcx, [rel __bss_end]
    sub rcx, rdi
    xor eax, eax
    rep stosb

    mov rsp, kernel_stack_top
    call kernel_main

    ; Nếu kernel_main lỡ return (không nên xảy ra ở bước này), dừng máy
    ; có kiểm soát thay vì để CPU chạy lung tung vào vùng nhớ rác.
    cli
.hang:
    hlt
    jmp .hang
