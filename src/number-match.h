#ifndef GNUMERIC_NUMBER_MATCH_H
#define GNUMERIC_NUMBER_MATCH_H

#include "gnumeric.h"

gboolean format_match_create  (StyleFormat *fmt);
void	 format_match_release (StyleFormat *fmt);

Value   *format_match_simple (char const *s);
Value   *format_match        (char const *s, StyleFormat *current_format,
			      StyleFormat **format);

void format_match_init   (void);
void format_match_finish (void);

#endif /* GNUMERIC_NUMBER_MATCH_H */
