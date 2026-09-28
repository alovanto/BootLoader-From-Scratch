#pragma once
#include <cstdint>

struct InterruptFrame;

// =========================================================================
// panic — điểm dừng có kiểm soát của kernel, tương đương KeBugCheckEx của
// Windows (màn hình xanh). Nguyên tắc: thà dừng hẳn kèm chẩn đoán đầy đủ còn
// hơn chạy tiếp với trạng thái đã hỏng rồi crash ở chỗ khác, nơi không còn
// dấu vết gì về nguyên nhân thật.
// =========================================================================
[[noreturn]] void panic(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// Bản dùng khi đang ở trong exception handler: in thêm toàn bộ thanh ghi đã
// chụp được tại thời điểm lỗi, cộng CR2/CR3.
[[noreturn]] void panic_frame(const InterruptFrame* frame, const char* fmt, ...)
    __attribute__((format(printf, 2, 3)));

// In chuỗi lời gọi hàm bằng cách đi ngược chuỗi RBP (cần -fno-omit-frame-pointer).
void backtrace(uint64_t rbp);

// Kiểm tra bất biến. Khác `assert` của libc: không bao giờ bị tắt bởi NDEBUG —
// trong kernel, một bất biến bị vi phạm gần như luôn dẫn tới hỏng dữ liệu âm
// thầm, nên dừng sớm luôn rẻ hơn.
// Bắt buộc có `msg`: khi bất biến vỡ lúc 2 giờ sáng, "%s" của biểu thức thường
// không đủ để nhớ ra nó đang bảo vệ điều gì.
#define KASSERT(cond, msg)                                                  \
    do {                                                                    \
        if (!(cond)) {                                                      \
            panic("KASSERT that bai: %s\n  ly do : %s\n  tai   : %s:%d",    \
                  #cond, (msg), __FILE__, __LINE__);                        \
        }                                                                   \
    } while (0)
