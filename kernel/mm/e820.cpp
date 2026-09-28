#include "mm/e820.hpp"
#include "lib/kprintf.hpp"

namespace {

// entry_count nằm ở 4 byte đầu buffer, mảng entry bắt đầu ngay sau đó —
// đúng layout mà detect_memory_e820 trong boot/boot.asm đã ghi.
uint32_t* const raw_entry_count = reinterpret_cast<uint32_t*>(E820_MAP_PHYS_ADDR);
E820Entry* const raw_entries    = reinterpret_cast<E820Entry*>(E820_MAP_PHYS_ADDR + 4);

const char* type_name(uint32_t type) {
    switch (static_cast<E820Type>(type)) {
    case E820Type::Usable:          return "Usable";
    case E820Type::Reserved:        return "Reserved";
    case E820Type::AcpiReclaimable: return "ACPI Reclaimable";
    case E820Type::AcpiNvs:         return "ACPI NVS";
    case E820Type::Bad:             return "Bad";
    default:                        return "Unknown";
    }
}

}  // namespace

uint32_t e820_entry_count() {
    return *raw_entry_count;
}

const E820Entry* e820_entry(uint32_t index) {
    return &raw_entries[index];
}

void e820_dump() {
    const uint32_t count = e820_entry_count();
    if (count == 0) {
        kprintf("  (khong co entry nao - BIOS khong ho tro E820?)\n");
        return;
    }

    uint64_t total_usable = 0;
    for (uint32_t i = 0; i < count; i++) {
        const E820Entry* entry = e820_entry(i);
        kprintf("  [%2u] base=%p  len=%p  %s\n",
                i,
                reinterpret_cast<void*>(entry->base),
                reinterpret_cast<void*>(entry->length),
                type_name(entry->type));
        if (entry->type == static_cast<uint32_t>(E820Type::Usable)) {
            total_usable += entry->length;
        }
    }
    // Chia cho 1024*1024 để ra MB — dùng phép chia số nguyên vì kernel không
    // được phép dùng số thực (build với -mgeneral-regs-only).
    kprintf("  tong RAM dung duoc: %lu MB\n", total_usable / (1024 * 1024));
}
