#ifndef util_h
#define util_h

#include <sys/types.h>
#include <stdarg.h>

// prototypes missing

size_t strlcpy(char *, const char *, size_t);
size_t strlcat(char *, const char *, size_t);
size_t strlen(const char *);
int strcmp(const char *, const char *);

void *memset(void *, int, size_t);
void *memcpy(void *, const void *, size_t);

void *pvPortMalloc(size_t);
int ets_vsnprintf(char *, size_t, const char *, va_list);

void ets_isr_attach(int, void *, void *);
void ets_isr_mask(unsigned intr);
void ets_isr_unmask(unsigned intr);

// local utility functions missing from libc

int snprintf(char *, size_t, const char *, ...) __attribute__ ((format (printf, 3, 4)));
void *malloc(size_t);
int atoi(const char *);

// other handy functions

void reset(void);

#endif
