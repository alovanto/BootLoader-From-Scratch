; =========================================================================
; gdt_flush.asm — nạp GDT mới và reload lại mọi segment register
;
; Vì sao cần file asm riêng: CPU không cho phép `mov cs, ax` trực tiếp —
; CS chỉ đổi được qua một cú nhảy far (far jump/far call/far return, hoặc
; iretq). Đây dùng kỹ thuật "far return" — push thủ công selector + địa
; chỉ đích lên stack rồi retfq — chuẩn khi cần đổi CS ở Long Mode.
; =========================================================================
BITS 64

; System V AMD64 ABI: rdi=arg1 (địa chỉ GdtPointer), rsi=arg2 (code selector),
; rdx=arg3 (data selector)
global gdt_flush
gdt_flush:
    lgdt [rdi]

    ; Data segment reload được bằng mov bình thường
    mov ax, dx
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; movzx đảm bảo rsi chỉ chứa đúng 16-bit selector, phần cao = 0 —
    ; tránh push rác từ phần cao của thanh ghi (System V ABI không đảm bảo
    ; phần cao của tham số uint16_t luôn sạch).
    movzx rsi, si
    push rsi                    ; selector đích (CS mới)
    lea rax, [rel .reload_cs]
    push rax                    ; địa chỉ đích (RIP mới)
    o64 retf                    ; far return 64-bit: pop RIP rồi pop CS cùng lúc
.reload_cs:
    ret
