#ifndef util_h
#define util_h

#include <sys/types.h>

size_t strlcpy(char *, const char *, size_t);
size_t strlcat(char *, const char *, size_t);
size_t strlen(const char *);
int strcmp(const char *, const char *);

void *memset(void *, int, size_t);
void *memcpy(void *, const void *, size_t);

int snprintf(char *, size_t, const char *, ...) __attribute__ ((format (printf, 3, 4)));

void *malloc(size_t);

int atoi(const char *);

void reset(void);

#endif
