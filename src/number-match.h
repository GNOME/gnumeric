#ifndef GNUMERIC_NUMBER_MATCH_H
#define GNUMERIC_NUMBER_MATCH_H

#include "gnumeric.h"

gboolean format_match_define (const char *format);
void     format_match_init   (void);
void     format_match_finish (void);
Value   *format_match_simple (const char *s);
Value   *format_match        (const char *s, StyleFormat **format);

#endif /* GNUMERIC_NUMBER_MATCH_H */
