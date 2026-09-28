; =========================================================================
; string.asm — memcpy/memset/memmove/memcmp
;
; Vì sao viết bằng assembly chứ không phải C++: g++ VẪN sinh lời gọi tới 4 hàm
; này ngay cả với -ffreestanding (khi copy struct, khởi tạo mảng...). Nếu tự
; viết memset bằng vòng lặp C++, ở -O2 trình biên dịch nhận ra "vòng lặp này
; chính là memset" và thay bằng... lời gọi memset → đệ quy vô hạn. Assembly
; loại bỏ hẳn khả năng đó.
;
; Quy ước System V AMD64: tham số rdi, rsi, rdx; giá trị trả về trong rax.
; DF (direction flag) được ABI đảm bảo bằng 0 khi vào hàm, và phải bằng 0 khi ra.
; =========================================================================
BITS 64

section .text

; void* memcpy(void* dst, const void* src, size_t n)
global memcpy
memcpy:
    mov rax, rdi            ; memcpy trả về con trỏ đích
    mov rcx, rdx
    rep movsb               ; CPU hiện đại có tối ưu sẵn cho rep movsb (ERMSB)
    ret

; void* memset(void* dst, int value, size_t n)
global memset
memset:
    mov r9, rdi             ; giữ con trỏ gốc: rdi bị rep stosb thay đổi
    mov eax, esi            ; giá trị cần điền nằm ở AL
    mov rcx, rdx
    rep stosb
    mov rax, r9
    ret

; void* memmove(void* dst, const void* src, size_t n) — an toàn khi hai vùng chồng lấn
global memmove
memmove:
    mov rax, rdi
    mov rcx, rdx
    cmp rdi, rsi
    jbe .forward            ; dst <= src: copy xuôi không bao giờ ghi đè byte chưa đọc
    lea r8, [rsi + rdx]
    cmp rdi, r8
    jae .forward            ; dst nằm hoàn toàn sau src: không chồng lấn
    ; Chồng lấn và dst > src → phải copy ngược từ cuối về đầu
    lea rsi, [rsi + rdx - 1]
    lea rdi, [rdi + rdx - 1]
    std
    rep movsb
    cld                     ; BẮT BUỘC trả DF về 0 trước khi return (ABI)
    ret
.forward:
    rep movsb
    ret

; int memcmp(const void* a, const void* b, size_t n)
global memcmp
memcmp:
    xor eax, eax
    mov rcx, rdx
    test rcx, rcx
    jz .done                ; n = 0 → hai vùng coi như bằng nhau
    repe cmpsb              ; so byte tại [rsi] với [rdi], dừng khi khác nhau
    je .done                ; ZF=1 nghĩa là chạy hết n byte mà vẫn bằng nhau
    ; cmpsb đã tăng cả rsi/rdi qua byte vừa so → lùi 1 để đọc lại đúng byte đó
    movzx eax, byte [rdi - 1]
    movzx ecx, byte [rsi - 1]
    sub eax, ecx            ; dấu của hiệu chính là kết quả memcmp
.done:
    ret
