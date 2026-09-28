#include "lib/kprintf.hpp"
#include "drivers/serial.hpp"
#include "drivers/vga_console.hpp"

namespace {

// In số không dấu theo cơ số bất kỳ, có hỗ trợ đệm cho đủ độ rộng.
// Buffer 32 byte là dư dả: số 64-bit ở cơ số 2 cũng chỉ cần 64 ký tự, còn ở
// cơ số nhỏ nhất ta dùng (8) là 22 ký tự.
void print_unsigned(uint64_t value, unsigned base, int width, char pad) {
    char buf[32];
    int len = 0;
    do {
        buf[len++] = "0123456789abcdef"[value % base];
        value /= base;
    } while (value != 0 && len < static_cast<int>(sizeof buf));

    while (len < width && len < static_cast<int>(sizeof buf)) {
        buf[len++] = pad;
    }
    // Chữ số được sinh ra từ hàng đơn vị trở lên nên phải in ngược lại
    while (len > 0) {
        console_putc(buf[--len]);
    }
}

}  // namespace

void console_putc(char c) {
    // Terminal mong đợi CRLF, còn VGA console tự xử lý '\n' của riêng nó
    if (c == '\n') serial_putc('\r');
    serial_putc(c);
    vga_console_putc(c);
}

void console_puts(const char* s) {
    while (*s != '\0') {
        console_putc(*s++);
    }
}

void vkprintf(const char* fmt, va_list ap) {
    for (; *fmt != '\0'; fmt++) {
        if (*fmt != '%') {
            console_putc(*fmt);
            continue;
        }

        fmt++;
        if (*fmt == '\0') break;  // chuỗi kết thúc ngay sau '%'

        char pad = ' ';
        int width = 0;
        bool is_long = false;

        if (*fmt == '0') {
            pad = '0';
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }
        while (*fmt == 'l') {  // chấp nhận cả %lx lẫn %llx
            is_long = true;
            fmt++;
        }

        switch (*fmt) {
        case 'd': {
            const int64_t value = is_long ? va_arg(ap, int64_t) : va_arg(ap, int);
            // Lấy trị tuyệt đối theo kiểu không dấu để INT64_MIN không bị tràn
            const uint64_t magnitude = value < 0 ? ~static_cast<uint64_t>(value) + 1
                                                 : static_cast<uint64_t>(value);
            if (value < 0) console_putc('-');
            print_unsigned(magnitude, 10, width, pad);
            break;
        }
        case 'u':
            print_unsigned(is_long ? va_arg(ap, uint64_t) : va_arg(ap, unsigned), 10, width, pad);
            break;
        case 'x':
            print_unsigned(is_long ? va_arg(ap, uint64_t) : va_arg(ap, unsigned), 16, width, pad);
            break;
        case 'p':
            console_puts("0x");
            print_unsigned(reinterpret_cast<uint64_t>(va_arg(ap, void*)), 16, 16, '0');
            break;
        case 's': {
            const char* s = va_arg(ap, const char*);
            console_puts(s != nullptr ? s : "(null)");
            break;
        }
        case 'c':
            console_putc(static_cast<char>(va_arg(ap, int)));
            break;
        case '%':
            console_putc('%');
            break;
        default:
            // Định dạng không hiểu: in nguyên văn để lỗi lộ ra thay vì im lặng
            console_putc('%');
            console_putc(*fmt);
            break;
        }
    }
}

void kprintf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vkprintf(fmt, ap);
    va_end(ap);
}
