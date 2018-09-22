#ifndef display_saa_h
#define display_saa_h

_Bool display_saa1064_init(void);
_Bool display_saa1064_bright(int brightness);
_Bool display_saa1064_set(const char *tag, const char *text);

#endif
