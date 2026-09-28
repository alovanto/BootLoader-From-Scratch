#include "lib/init.hpp"
#include "lib/panic.hpp"

// =========================================================================
// Phần runtime tối thiểu mà trình biên dịch C++ ngầm yêu cầu phải tồn tại.
// Trong chương trình bình thường, chúng do libstdc++ cung cấp; kernel chạy
// freestanding nên phải tự cung cấp, nếu không sẽ lỗi link kiểu
// "undefined reference to __cxa_atexit".
// =========================================================================

extern "C" {

// Được gọi nếu một hàm ảo thuần tuý bị gọi (thường do dùng object khi
// constructor/destructor của nó chưa chạy xong) — luôn là bug.
void __cxa_pure_virtual() {
    panic("goi ham ao thuan tuy (pure virtual call)");
}

// g++ đăng ký destructor của object toàn cục qua __cxa_atexit để chúng chạy
// khi chương trình kết thúc. Kernel không bao giờ kết thúc, nên chỉ cần nhận
// rồi bỏ qua. Trả 0 = "đăng ký thành công".
int __cxa_atexit(void (*destructor)(void*), void* arg, void* dso_handle) {
    (void)destructor;
    (void)arg;
    (void)dso_handle;
    return 0;
}

// Symbol định danh "shared object" chứa các destructor trên. Với kernel chỉ
// cần tồn tại, giá trị không quan trọng.
void* __dso_handle = nullptr;

}  // extern "C"

// -------------------------------------------------------------------------
// Constructor của object toàn cục
//
// Trình biên dịch gom con trỏ hàm khởi tạo của mọi object toàn cục có
// constructor vào section .init_array. Trong chương trình bình thường, crt0
// của libc duyệt mảng này trước khi gọi main(). Kernel không có crt0 nên phải
// tự duyệt — nếu quên, mọi object toàn cục sẽ ở trạng thái chưa khởi tạo mà
// không có dấu hiệu gì rõ ràng.
// -------------------------------------------------------------------------
using Constructor = void (*)();

extern "C" Constructor __init_array_start[];
extern "C" Constructor __init_array_end[];

void run_global_constructors() {
    for (Constructor* ctor = __init_array_start; ctor != __init_array_end; ++ctor) {
        (*ctor)();
    }
}
