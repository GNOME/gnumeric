#ifndef FORMAT_H_
#define FORMAT_H_

void   format_destroy (StyleFormat *format);
void   format_compile (StyleFormat *format);
gchar *format_value   (StyleFormat *format, Value *value, char **color_name);

#endif
