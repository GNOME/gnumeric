#ifndef GNUMERIC_FORMAT_H
#define GNUMERIC_FORMAT_H

#include "gnumeric.h"
#include <sys/types.h>
#include <regex.h>

struct _StyleFormat {
	int    ref_count;
	char  *format;
        GSList *entries;  /* Of type StyleFormatEntry. */
	char        *regexp_str;
	GByteArray  *match_tags;
	regex_t     regexp;
};

char	      *style_format_delocalize  (char const *descriptor_string);
StyleFormat   *style_format_new_XL	(char const *descriptor_string,
					 gboolean delocalize);
char   	      *style_format_as_XL	(StyleFormat const *fmt,
					 gboolean localized);
char   	      *style_format_str_as_XL	(char const *descriptor_string,
					 gboolean localized);

void           style_format_ref		(StyleFormat *sf);
void           style_format_unref	(StyleFormat *sf);
gboolean       style_format_is_general	(StyleFormat const *sf);
gboolean       style_format_is_text	(StyleFormat const *sf);

void   format_destroy (StyleFormat *format);
char  *format_value   (StyleFormat *format, Value const *value, StyleColor **color,
		       float col_width);

void   format_color_init     (void);
void   format_color_shutdown (void);

char  *format_add_decimal      (StyleFormat const *fmt);
char  *format_remove_decimal   (StyleFormat const *fmt);

/* Locale support routines */
char const *gnumeric_setlocale      (int category, char const *val);
char const *format_get_currency     (gboolean *precedes, gboolean *space_sep);
gboolean    format_month_before_day (void);
char        format_get_arg_sep      (void);
char        format_get_col_sep      (void);
char        format_get_thousand     (void);
char        format_get_decimal      (void);

void number_format_init (void);
void number_format_shutdown (void);

#endif /* GNUMERIC_FORMAT_H */
