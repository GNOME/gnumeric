#ifndef GOFFICE_FORMAT_H
#define GOFFICE_FORMAT_H

#include <goffice/utils/goffice-utils.h>
#include <pango/pango-attributes.h>
#include <goffice/cut-n-paste/pcre/pcreposix.h>
#include <glib.h>

G_BEGIN_DECLS

typedef enum {
	FMT_UNKNOWN	= -1,

	FMT_GENERAL	= 0,
	FMT_NUMBER	= 1,
	FMT_CURRENCY	= 2,
	FMT_ACCOUNT	= 3,
	FMT_DATE	= 4,
	FMT_TIME	= 5,
	FMT_PERCENT	= 6,
	FMT_FRACTION	= 7,
	FMT_SCIENCE	= 8,
	FMT_TEXT	= 9,
	FMT_SPECIAL	= 10,

	FMT_MARKUP	= 11	/* Internal use only */
} FormatFamily;

typedef struct {
	unsigned char const * const symbol;
	unsigned char const * const description;
	gboolean const precedes;
	gboolean const has_space;
} CurrencySymbol;

typedef struct {
	gboolean thousands_sep;
	int	 num_decimals;	/* 0 - 30 */
	int	 negative_fmt;	/* 0 - 3 */
	int	 currency_symbol_index;
	int	 list_element;
	gboolean date_has_days;
	gboolean date_has_months;
	int      fraction_denominator;
} FormatCharacteristics;

struct _GOFormat {
	int                   ref_count;
	char                 *format;
        GSList               *entries;  /* Of type StyleFormatEntry. */
	char                 *regexp_str;
	GByteArray           *match_tags;
	go_regex_t	      regexp;
	FormatFamily          family;
	FormatCharacteristics family_info;
	gboolean	      is_var_width;
	PangoAttrList	     *markup; /* only for FMT_MARKUP */
};

char	    *style_format_delocalize	(char const *descriptor_string);
GOFormat   *style_format_new_markup	(PangoAttrList *markup, gboolean add_ref);
GOFormat   *style_format_new_XL	(char const *descriptor_string,
					 gboolean delocalize);
GOFormat   *style_format_build		(FormatFamily family,
					 FormatCharacteristics const *info);

char   	      *style_format_as_XL	(GOFormat const *fmt,
					 gboolean localized);
char   	      *style_format_str_as_XL	(char const *descriptor_string,
					 gboolean localized);

void           style_format_ref		(GOFormat *sf);
void           style_format_unref	(GOFormat *sf);
gboolean       style_format_equal       (GOFormat const *a, GOFormat const *b);

GOFormat *style_format_general		   (void);
GOFormat *style_format_default_date	   (void);
GOFormat *style_format_default_time	   (void);
GOFormat *style_format_default_date_time  (void);
GOFormat *style_format_default_percentage (void);
GOFormat *style_format_default_money	   (void);

#define style_format_is_general(fmt)	((fmt)->family == FMT_GENERAL)
#define style_format_is_markup(fmt)	((fmt)->family == FMT_MARKUP)
#define style_format_is_text(fmt)	((fmt)->family == FMT_TEXT)
#define style_format_is_var_width(fmt)	((fmt)->is_var_width)

void   format_color_init     (void);
void   format_color_shutdown (void);

GOFormat *format_add_decimal      (GOFormat const *fmt);
GOFormat *format_remove_decimal   (GOFormat const *fmt);
GOFormat *format_toggle_thousands (GOFormat const *fmt);

typedef struct {
	int  right_optional, right_spaces, right_req, right_allowed;
	int  left_spaces, left_req;
	float scale;
	gboolean rendered;
	gboolean decimal_separator_seen;
	gboolean group_thousands;
	gboolean has_fraction;
} format_info_t;
void render_number (GString *result, double number, format_info_t const *info);

/* Locale support routines */
void	       gnm_set_untranslated_bools (void);
char const *   gnm_setlocale           (int category, char const *val);
GString const *format_get_currency     (gboolean *precedes, gboolean *space_sep);
gboolean       format_month_before_day (void);
char           format_get_arg_sep      (void);
char           format_get_col_sep      (void);
GString const *format_get_thousand     (void);
GString const *format_get_decimal      (void);
char const *   format_boolean          (gboolean b);

void number_format_init (void);
void number_format_shutdown (void);

void currency_date_format_init     (void);
void currency_date_format_shutdown (void);

FormatFamily cell_format_classify (GOFormat const *fmt, FormatCharacteristics *info);

/* Indexed by FormatCharacteristics */
extern char const * const * const cell_formats [];

extern CurrencySymbol const currency_symbols [];
extern char const * const day_short   [];
extern char const * const day_long    [];
extern char const * const month_short [];
extern char const * const month_long  [];

G_END_DECLS

#endif /* GOFFICE_FORMAT_H */
