#ifndef display_orbital_h
#define display_orbital_h

bool_t display_orbital_init(void);
bool_t display_orbital_bright(int brightness);
bool_t display_orbital_set(const char *tag, const char *text);
bool_t display_orbital_show(void);

#endif
