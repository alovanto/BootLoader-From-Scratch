#pragma once

// Chạy constructor của mọi object toàn cục (section .init_array).
// Gọi sớm trong kernel_main, nhưng SAU serial_init() để nếu constructor nào
// panic thì còn thấy được thông báo.
void run_global_constructors();
