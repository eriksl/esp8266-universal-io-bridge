#ifndef display_saa_h
#define display_saa_h

bool_t display_saa1064_init(void);
bool_t display_saa1064_bright(int brightness);
bool_t display_saa1064_set(const char *tag, const char *text);

#endif
