#ifndef GNUMERIC_NUMBER_MATCH_H
#define GNUMERIC_NUMBER_MATCH_H

#include "numbers.h"

int      format_match_define (const char *format);
void     format_match_init   (void);
void     format_match_finish (void);
gboolean format_match        (const char *s, float_t *v, char **format);

#endif /* GNUMERIC_NUMBER_MATCH_H */
