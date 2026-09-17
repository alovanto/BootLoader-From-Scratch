#include "pmm.hpp"
#include "e820.hpp"

// Từ linker.ld — địa chỉ ngay sau toàn bộ ảnh kernel (.text/.rodata/.data/
// .bss, bao gồm cả chính bitmap bên dưới). PMM dùng để tự khoá vùng của
// chính mình, không cấp đè lên code/data đang chạy.
extern "C" uint8_t __kernel_end[];

namespace {

volatile uint16_t* const VGA = reinterpret_cast<volatile uint16_t*>(0xB8000);
constexpr int VGA_COLS = 80;

// 4GB / 4096 byte-mỗi-frame / 8 bit-mỗi-byte = 131072 byte (128KB).
// Nằm trong .bss — được zero-hoá bởi vòng lặp trong kernel_entry.asm,
// KHÔNG dựa vào việc file trên đĩa có zero sẵn hay không (xem comment ở đó).
constexpr uint64_t BITMAP_SIZE_BYTES = MAX_FRAMES / 8;
uint8_t bitmap[BITMAP_SIZE_BYTES];

uint64_t free_count = 0;
uint64_t total_usable_at_init = 0;

// Gợi ý điểm bắt đầu tìm frame trống tiếp theo — tránh phải quét lại từ
// đầu bitmap mỗi lần cấp phát khi phần đầu bitmap đã kín.
uint64_t next_free_hint = 0;

inline bool is_used(uint64_t frame_index) {
    return (bitmap[frame_index / 8] & (1u << (frame_index % 8))) != 0;
}

inline void set_used(uint64_t frame_index) {
    bitmap[frame_index / 8] |= static_cast<uint8_t>(1u << (frame_index % 8));
}

inline void set_free(uint64_t frame_index) {
    bitmap[frame_index / 8] &= static_cast<uint8_t>(~(1u << (frame_index % 8)));
}

inline uint64_t align_up(uint64_t value, uint64_t align) {
    return (value + align - 1) & ~(align - 1);
}

inline uint64_t align_down(uint64_t value, uint64_t align) {
    return value & ~(align - 1);
}

void vga_print(int row, const char* str, uint8_t color) {
    int col = 0;
    while (*str && col < VGA_COLS) {
        VGA[row * VGA_COLS + col] = static_cast<uint16_t>(color) << 8 | static_cast<uint8_t>(*str);
        str++;
        col++;
    }
}

void append(char*& p, const char* s) {
    while (*s) {
        *p++ = *s++;
    }
}

void append_hex(char*& p, uint64_t value) {
    const char* digits = "0123456789ABCDEF";
    *p++ = '0';
    *p++ = 'x';
    for (int i = 15; i >= 0; i--) {
        *p++ = digits[(value >> (i * 4)) & 0xF];
    }
}

} // namespace

void pmm_init() {
    // Bước 1: mặc định MỌI frame trong phạm vi hỗ trợ là "đã dùng" — an
    // toàn theo thiết kế, chỉ mở khoá đúng phần đã xác nhận Usable.
    for (uint64_t i = 0; i < BITMAP_SIZE_BYTES; i++) {
        bitmap[i] = 0xFF;
    }

    // Bước 2: mở khoá (đánh dấu trống) các frame nằm trong vùng Usable theo
    // bản đồ E820 mà boot.asm đã dò và kernel/mm/e820.cpp đọc lại.
    const uint32_t count = e820_entry_count();
    for (uint32_t i = 0; i < count; i++) {
        const E820Entry* e = e820_entry(i);
        if (e->type != static_cast<uint32_t>(E820Type::Usable)) {
            continue;
        }

        uint64_t start = align_up(e->base, FRAME_SIZE);
        uint64_t end   = align_down(e->base + e->length, FRAME_SIZE);
        if (end > MAX_SUPPORTED_RAM) {
            end = MAX_SUPPORTED_RAM;
        }

        for (uint64_t addr = start; addr < end; addr += FRAME_SIZE) {
            set_free(addr / FRAME_SIZE);
        }
    }

    // Bước 3: khoá LẠI toàn bộ 1MB thấp — bất kể E820 nói gì về vùng này.
    // Đây là nơi chứa BIOS data area, tàn dư Stage1/Stage2, bản kernel.bin
    // TẠM còn sót ở 0x10000, và chính bảng E820 ta vừa đọc ở 0x20000.
    // Cấp phát đè lên bất kỳ chỗ nào trong số này sẽ gây lỗi rất khó truy.
    const uint64_t low_mb_frames = 0x100000ULL / FRAME_SIZE;
    for (uint64_t i = 0; i < low_mb_frames && i < MAX_FRAMES; i++) {
        set_used(i);
    }

    // Bước 4: khoá vùng kernel thật đang chạy (0x100000 tới __kernel_end),
    // bao gồm cả chính bitmap này — không được tự cấp đè lên chính mình.
    const uint64_t kernel_start_frame = 0x100000ULL / FRAME_SIZE;
    const uint64_t kernel_end_frame =
        align_up(reinterpret_cast<uint64_t>(__kernel_end), FRAME_SIZE) / FRAME_SIZE;
    for (uint64_t i = kernel_start_frame; i < kernel_end_frame && i < MAX_FRAMES; i++) {
        set_used(i);
    }

    // Đếm lại số frame thật sự trống sau khi đã khoá các vùng đặc biệt ở trên.
    free_count = 0;
    for (uint64_t i = 0; i < MAX_FRAMES; i++) {
        if (!is_used(i)) {
            free_count++;
        }
    }
    total_usable_at_init = free_count;
    next_free_hint = 0;
}

uint64_t pmm_alloc_frame() {
    // Quét từ hint tới cuối, rồi vòng lại từ đầu tới hint — đảm bảo không
    // bỏ sót frame trống nào dù nó nằm trước điểm hint hiện tại (ví dụ vừa
    // được free() trả lại).
    for (uint64_t i = next_free_hint; i < MAX_FRAMES; i++) {
        if (!is_used(i)) {
            set_used(i);
            next_free_hint = i + 1;
            free_count--;
            return i * FRAME_SIZE;
        }
    }
    for (uint64_t i = 0; i < next_free_hint; i++) {
        if (!is_used(i)) {
            set_used(i);
            next_free_hint = i + 1;
            free_count--;
            return i * FRAME_SIZE;
        }
    }
    return 0; // Hết bộ nhớ trong phạm vi hỗ trợ.
}

void pmm_free_frame(uint64_t phys_addr) {
    const uint64_t index = phys_addr / FRAME_SIZE;
    if (index >= MAX_FRAMES) {
        return; // Ngoài phạm vi quản lý — bỏ qua an toàn.
    }
    if (!is_used(index)) {
        return; // Bảo vệ double-free: đã trống rồi thì không đếm nhầm.
    }
    set_free(index);
    free_count++;
    if (index < next_free_hint) {
        next_free_hint = index; // Ưu tiên tái sử dụng ngay chỗ vừa trả lại.
    }
}

uint64_t pmm_total_usable_frames() {
    return total_usable_at_init;
}

uint64_t pmm_free_frames() {
    return free_count;
}

void pmm_dump(int row) {
    char line[80];
    char* p = line;
    append(p, "PMM: tong frame usable=");
    append_hex(p, pmm_total_usable_frames());
    append(p, " con trong=");
    append_hex(p, pmm_free_frames());
    *p = '\0';
    vga_print(row, line, 0x0B);
}
