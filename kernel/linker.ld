/* linker.ld — đặt kernel bắt đầu tại 1MB (0x100000), vùng RAM thấp
   thường an toàn và đã được identity-map bởi boot.asm.

   GHI CHÚ: khi chuyển kernel lên higher-half (0xFFFFFFFF80100000), file này
   là chỗ sửa đầu tiên — dùng VMA cao kèm `AT()` để LMA vẫn là 0x100000.
   Xem docs/architecture.md mục 6.2. */
ENTRY(_start)

SECTIONS
{
    . = 0x100000;
    __kernel_start = .;

    .text : {
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        /* .init_array chứa con trỏ tới constructor của mọi object toàn cục.
           Không khai báo tường minh ở đây thì `ld` sẽ tự xếp nó như một
           "orphan section" — có thể rơi ra ngoài vùng bootloader copy, và
           lib/cxx_runtime.cpp sẽ không tìm thấy symbol đầu/cuối để duyệt. */
        . = ALIGN(8);
        __init_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        __init_array_end = .;

        *(.data*)
    }

    .bss : {
        __bss_start = .;
        *(COMMON)
        *(.bss*)
        __bss_end = .;
    }

    /* Địa chỉ ngay sau toàn bộ ảnh kernel (.text/.rodata/.data/.bss) —
       Physical Frame Allocator (mm/pmm.cpp) dùng để khoá vùng kernel đang
       chạy, tự động bao gồm cả bitmap của chính nó vì bitmap nằm trong .bss. */
    . = ALIGN(4096);
    __kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame*)
        *(.note*)
    }
}
