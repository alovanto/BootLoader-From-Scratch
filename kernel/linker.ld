/* linker.ld — đặt kernel bắt đầu tại 1MB (0x100000), vùng RAM thấp
   thường an toàn và đã được identity-map bởi boot.asm. */
ENTRY(_start)

SECTIONS
{
    . = 0x100000;

    .text : {
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
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
    __kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame*)
        *(.note*)
    }
}
