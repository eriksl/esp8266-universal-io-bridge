#ifndef attribute_h
#define attribute_h

#define irom __attribute__((section(".irom0.text")))
#define iram __attribute__((section(".text")))
#define roflash __attribute__((section(".flash.rodata")))
#define attr_flash_align __attribute__((aligned(4)))
#define attr_inline inline __attribute__((always_inline)) __attribute__((flatten)) static
#define attr_unused __attribute__ ((unused))
#define attr_pure __attribute__ ((pure))
#define attr_const __attribute__ ((const))
#define attr_packed __attribute__ ((__packed__))
#define attr_speed __attribute__ ((optimize("O3", "unroll-loops","move-loop-invariants","tree-copy-prop","tree-ccp","tree-tail-merge")))
#define attr_nonnull __attribute__ ((nonnull))
#define assert_size(type, size) _Static_assert(sizeof(type) == size, "sizeof(" #type ") != " #size)
#define assert_enum(name, value) _Static_assert((name) == (value), "enum value for " #name " != " #value)

#endif
