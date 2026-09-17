#pragma once
#include <cstdint>

constexpr uint64_t FRAME_SIZE = 4096;

// Giới hạn RAM tối đa mà allocator này quản lý được — quyết định kích thước
// bitmap tĩnh (4GB / 4096 / 8 = 128KB, nằm trong .bss). Đây là mức "vừa
// phải": đủ dư dả cho một máy ảo QEMU thông thường, không lãng phí như cấp
// phát cho toàn bộ không gian địa chỉ 64-bit. RAM thật vượt quá mức này
// (hiếm ở giai đoạn hobby OS) sẽ bị bỏ qua an toàn — không cấp phát được
// phần vượt, nhưng cũng không gây lỗi.
constexpr uint64_t MAX_SUPPORTED_RAM = 4ULL * 1024 * 1024 * 1024; // 4GB
constexpr uint64_t MAX_FRAMES        = MAX_SUPPORTED_RAM / FRAME_SIZE;

// Đọc bản đồ E820 (đã có sẵn từ boot.asm, qua mm/e820.hpp) và dựng bitmap:
// mặc định MỌI frame là "đã dùng", chỉ mở khoá đúng những frame nằm trong
// vùng Usable — rồi khoá LẠI toàn bộ 1MB thấp (BIOS data area, tàn dư
// Stage1/Stage2, bảng E820) và khoá vùng kernel đang chạy thật
// (0x100000 tới __kernel_end, tự động bao gồm cả chính bitmap này).
// PHẢI gọi sau khi E820 đã sẵn sàng — không có thứ tự nào khác hợp lệ.
void pmm_init();

// Cấp một frame vật lý trống (4KB, đã căn chỉnh sẵn theo FRAME_SIZE), trả
// về địa chỉ vật lý của nó. Trả về 0 nếu hết bộ nhớ trong phạm vi hỗ trợ —
// 0 không bao giờ là frame hợp lệ để cấp vì vùng đó luôn bị khoá cứng
// (BIOS data area, xem pmm_init()).
uint64_t pmm_alloc_frame();

// Trả lại một frame đã cấp trước đó cho pmm_alloc_frame(). Gọi với địa chỉ
// chưa từng cấp, hoặc đã free rồi (double-free), sẽ bị bỏ qua an toàn thay
// vì phá hỏng bitmap hoặc đếm sai free_count.
void pmm_free_frame(uint64_t phys_addr);

uint64_t pmm_total_usable_frames();
uint64_t pmm_free_frames();

// In tổng quan bitmap (tổng frame usable, còn trống) ra VGA text buffer, để
// xác nhận bằng mắt trên QEMU — đúng nguyên tắc "mỗi bước phải thấy kết quả
// trên QEMU" ở CLAUDE.md, trước khi VMM (bước tiếp theo) dùng PMM làm nền.
void pmm_dump(int row);
