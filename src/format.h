#ifndef FORMAT_H_
#define FORMAT_H_

void   format_destroy (StyleFormat *format);
void   format_compile (StyleFormat *format);
gchar *format_value   (StyleFormat *format, Value *value, StyleColor **color);

void   format_color_init     (void);
void   format_color_shutdown (void);
#endif
