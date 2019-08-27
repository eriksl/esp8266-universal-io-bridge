#ifndef attribute_h
#define attribute_h

#define iram __attribute__((section(".iram.text"))) __attribute__ ((optimize("Os")))
#define roflash __attribute__((section(".flash.rodata")))
#define fallthrough __attribute__((fallthrough))
#define attr_flash_align __attribute__((aligned(4)))
#define attr_align_int __attribute__((aligned(sizeof(int))))
#define attr_inline inline __attribute__((always_inline)) __attribute__((flatten)) static
#define attr_unused __attribute__ ((unused))
#define attr_used __attribute__ ((used))
#define attr_pure __attribute__ ((pure))
#define attr_const __attribute__ ((const))
#define attr_packed __attribute__ ((__packed__))
#define attr_nonnull __attribute__ ((nonnull))
#define attr_result_used __attribute__ ((warn_unused_result))
#define assert_size(type, size) _Static_assert(sizeof(type) == size, "sizeof(" #type ") != " #size)
#define assert_enum(name, value) _Static_assert((name) == (value), "enum value for " #name " != " #value)

#endif
