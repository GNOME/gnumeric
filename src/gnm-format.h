#ifndef GNM_FORMAT_H
#define GNM_FORMAT_H

#include "gnumeric.h"
#include <goffice/utils/go-format.h>
#include <pango/pango.h>

char  *format_value	    (GOFormat const *format,
			     GnmValue const *value, GOColor *go_color,
			     int col_width,
			     GODateConventions const *date_conv);

GOFormatNumberError format_value_gstring (GString *result,
					  GOFormat const *format,
					  GnmValue const *value,
					  GOColor *go_color,
					  int col_width,
					  GODateConventions const *date_conv);

GOFormatNumberError gnm_format_layout    (PangoLayout *result,
					  GOFontMetrics *metrics,
					  GOFormat const *format,
					  GnmValue const *value,
					  GOColor *go_color,
					  int col_width,
					  GODateConventions const *date_conv,
					  gboolean unicode_minus);

/*
 * http://www.unicode.org/charts/PDF/U0080.pdf
 * http://www.unicode.org/charts/PDF/U2000.pdf
 * http://www.unicode.org/charts/PDF/U20A0.pdf
 * http://www.unicode.org/charts/PDF/U2200.pdf
 */
#define UNICODE_LOGICAL_NOT_C 0x00AC
#define UNICODE_ZERO_WIDTH_SPACE_C 0X200B
#define UNICODE_EURO_SIGN_C 0x20AC
#define UNICODE_MINUS_SIGN_C 0x2212
#define UNICODE_DIVISION_SLASH_C 0x2215
#define UNICODE_LOGICAL_AND_C 0x2227
#define UNICODE_LOGICAL_OR_C 0x2228
#define UNICODE_NOT_EQUAL_TO_C 0x2260
#define UNICODE_LESS_THAN_OR_EQUAL_TO_C 0x2264
#define UNICODE_GREATER_THAN_OR_EQUAL_TO_C 0x2265



#endif /* GNM_FORMAT_H */
