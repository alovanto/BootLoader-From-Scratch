#pragma once
#include <cstddef>

// Cài đặt thật nằm trong lib/string.asm (xem lý do phải dùng assembly ở đó).
// Khai báo extern "C" để tên symbol khớp đúng với những gì g++ tự sinh lời gọi.
extern "C" {
void* memcpy(void* dst, const void* src, size_t n);
void* memset(void* dst, int value, size_t n);
void* memmove(void* dst, const void* src, size_t n);
int   memcmp(const void* a, const void* b, size_t n);
}
