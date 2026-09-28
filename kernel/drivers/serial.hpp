#pragma once

// Driver COM1 tối giản, chạy bằng polling (không dùng ngắt).
//
// Vì sao serial quan trọng hơn vẻ ngoài của nó: VGA text buffer chỉ có 80x25 ô,
// không cuộn lại được, không copy-paste được, và bị xoá sạch mỗi khi có
// exception in chẩn đoán đè lên. Serial thì QEMU chuyển thẳng ra terminal
// (`-serial stdio`), giữ lại toàn bộ lịch sử, grep được, lưu vào file được.
// Mọi bước sau (VMM, heap, scheduler) đều debug qua đây.
void serial_init();
void serial_putc(char c);
