#ifndef GNUMERIC_FORMAT_H
#define GNUMERIC_FORMAT_H

#include "gnumeric.h"

char	      *style_format_delocalize  (const char *descriptor_string);
StyleFormat   *style_format_new_XL	(const char *descriptor_string,
					 gboolean delocalize);
char   	      *style_format_as_XL	(StyleFormat const *fmt,
					 gboolean localized);
char   	      *style_format_str_as_XL	(const char *descriptor_string,
					 gboolean localized);

void           style_format_ref		(StyleFormat *sf);
void           style_format_unref	(StyleFormat *sf);
gboolean       style_format_is_general	(StyleFormat const *sf);
				      
void   format_destroy (StyleFormat *format);
char  *format_value   (StyleFormat *format, const Value *value, StyleColor **color,
		       char const * entered_text);

void   format_color_init     (void);
void   format_color_shutdown (void);

char  *format_toggle_thousands (StyleFormat const *fmt);
char  *format_add_decimal      (StyleFormat const *fmt);
char  *format_remove_decimal   (StyleFormat const *fmt);

/* Locale support routines */
char const *gnumeric_setlocale      (int category, char const *val);
char const *format_get_currency     (void);
gboolean    format_month_before_day (void);
char        format_get_arg_sep      (void);
char        format_get_col_sep      (void);
char        format_get_thousand     (void);
char        format_get_decimal      (void);

void number_format_init (void);
void number_format_shutdown (void);

#endif /* GNUMERIC_FORMAT_H */
