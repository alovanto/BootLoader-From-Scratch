#pragma once
#include <cstdint>

// =========================================================================
// PIC 8259 — bộ điều khiển ngắt phần cứng đời đầu (vẫn còn trên mọi PC).
//
// CÁI BẪY LỚN NHẤT: mặc định PIC ánh xạ IRQ0-7 vào vector 8-15 của CPU, mà
// vector 8 chính là #DF (Double Fault), 13 là #GP, 14 là #PF. Nghĩa là khi
// ngắt được bật mà chưa remap, một tick timer bình thường sẽ trông y hệt một
// Double Fault — chẩn đoán dẫn đi sai hướng hoàn toàn.
//
// Vì vậy: remap sang vector 32-47 (ngoài vùng 0-31 mà Intel dành riêng cho
// exception) và mask toàn bộ IRQ NGAY TRONG quá trình khởi động kernel, trước
// khi có bất kỳ khả năng nào ngắt được bật.
// =========================================================================

// Vector CPU tương ứng IRQ0 sau khi remap. IRQ N → vector PIC_VECTOR_BASE + N.
constexpr uint8_t PIC_VECTOR_BASE = 32;

// Remap sang 32-47 rồi mask toàn bộ 16 IRQ. Driver tự gọi pic_unmask khi sẵn sàng.
void pic_remap_and_mask_all();

void pic_mask(uint8_t irq);
void pic_unmask(uint8_t irq);

// Báo cho PIC biết đã xử lý xong ngắt (End Of Interrupt). Thiếu bước này thì
// PIC sẽ không bao giờ phát IRQ đó lần nữa.
void pic_send_eoi(uint8_t irq);

// IRQ7/IRQ15 có thể là ngắt "ma": PIC phát tín hiệu rồi rút lại trước khi CPU
// kịp đọc. Phân biệt bằng In-Service Register; ngắt ma thì KHÔNG gửi EOI.
bool pic_is_spurious(uint8_t irq);
