#ifndef GNUMERIC_FORMAT_H
#define GNUMERIC_FORMAT_H

#include "style.h"
#include "expr.h"

void   format_destroy (StyleFormat *format);
void   format_compile (StyleFormat *format);
gchar *format_value   (StyleFormat *format, Value *value, StyleColor **color);

void   format_color_init     (void);
void   format_color_shutdown (void);
#endif /* GNUMERIC_FORMAT_H */
