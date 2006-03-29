#ifndef GNUMERIC_NUMBER_MATCH_H
#define GNUMERIC_NUMBER_MATCH_H

#include "gnumeric.h"

GnmValue   *format_match_simple (char const *s);
GnmValue   *format_match        (char const *s, GOFormat *cur_fmt,
				 GODateConventions const *date_conv);
GnmValue   *format_match_number (char const *s, GOFormat *cur_fmt,
				 GODateConventions const *date_conv);

#endif /* GNUMERIC_NUMBER_MATCH_H */
