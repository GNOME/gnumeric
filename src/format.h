#ifndef GNUMERIC_FORMAT_H
#define GNUMERIC_FORMAT_H

#include "style.h"
#include "expr.h"

void   format_destroy (StyleFormat *format);
void   format_compile (StyleFormat *format);
gchar *format_value   (StyleFormat *format, const Value *value, StyleColor **color);

void   format_color_init     (void);
void   format_color_shutdown (void);

char  *format_add_thousand   (const char *format);
char  *format_add_decimal    (const char *format);
char  *format_remove_decimal (const char *format);

char  *format_get_thousand   (void);
char  *format_get_decimal    (void);

#endif /* GNUMERIC_FORMAT_H */
