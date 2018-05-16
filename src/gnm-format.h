#ifndef _GNM_FORMAT_H_
# define _GNM_FORMAT_H_

#include <gnumeric.h>
#include <goffice/goffice.h>
#include <pango/pango.h>

G_BEGIN_DECLS

char  *format_value	    (GOFormat const *format,
			     GnmValue const *value,
			     int col_width,
			     GODateConventions const *date_conv);

GOFormatNumberError format_value_gstring (GString *str,
					  GOFormat const *format,
					  GnmValue const *value,
					  int col_width,
					  GODateConventions const *date_conv);
GOFormatNumberError format_value_layout (PangoLayout *layout,
					 GOFormat const *format,
					 GnmValue const *value,
					 int col_width,
					 GODateConventions const *date_conv);

GOFormatNumberError gnm_format_layout    (PangoLayout *result,
					  GOFontMetrics *metrics,
					  GOFormat const *format,
					  GnmValue const *value,
					  int col_width,
					  GODateConventions const *date_conv,
					  gboolean unicode_minus);

GOFormat const * gnm_format_specialize (GOFormat const *fmt,
					GnmValue const *value);

int gnm_format_is_date_for_value (GOFormat const *fmt,
				  GnmValue const *value);
int gnm_format_is_time_for_value (GOFormat const *fmt,
				  GnmValue const *value);

int gnm_format_month_before_day (GOFormat const *fmt,
				 GnmValue const *value);

char *gnm_format_frob_slashes (const char *s);

GOFormat *gnm_format_for_date_editing (GnmCell const *cell);

gboolean gnm_format_has_hour (GOFormat const *fmt,
			      GnmValue const *value);

typedef enum {
	GNM_FORMAT_IMPORT_NULL_INVALID,
	GNM_FORMAT_IMPORT_PATCHUP_INCOMPLETE
} GnmFormatImportFlags;

GOFormat *gnm_format_import (const char *fmt,
			     GnmFormatImportFlags flags);

/*
 * http://www.unicode.org/charts/PDF/U0080.pdf
 * http://www.unicode.org/charts/PDF/U2000.pdf
 * http://www.unicode.org/charts/PDF/U20A0.pdf
 * http://www.unicode.org/charts/PDF/U2200.pdf
 */
#define UNICODE_LOGICAL_NOT_C 0x00AC
#define UNICODE_ZERO_WIDTH_SPACE_C 0X200B
#define UNICODE_ZERO_WIDTH_SPACE_C_UTF8_LENGTH 3
#define UNICODE_EURO_SIGN_C 0x20AC
#define UNICODE_MINUS_SIGN_C 0x2212
#define UNICODE_DIVISION_SLASH_C 0x2215
#define UNICODE_LOGICAL_AND_C 0x2227
#define UNICODE_LOGICAL_OR_C 0x2228
#define UNICODE_NOT_EQUAL_TO_C 0x2260
#define UNICODE_LESS_THAN_OR_EQUAL_TO_C 0x2264
#define UNICODE_GREATER_THAN_OR_EQUAL_TO_C 0x2265

G_END_DECLS

#endif /* _GNM_FORMAT_H_ */
