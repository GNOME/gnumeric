#ifndef GNUMERIC_FORMATS_H
#define GNUMERIC_FORMATS_H

typedef enum
{
    FMT_UNKNOWN = -1,

    FMT_GENERAL	 = 0,
    FMT_NUMBER	 = 1,
    FMT_CURRENCY = 2,
    FMT_ACCOUNT	 = 3,
    FMT_DATE	 = 4,
    FMT_TIME	 = 5,
    FMT_PERCENT	 = 6,
    FMT_FRACTION = 7,
    FMT_SCIENCE	 = 8,
    FMT_TEXT	 = 9,
    FMT_SPECIAL  = 10
} FormatFamily;

typedef struct
{
	gint	 catalog_element;

	gboolean thousands_sep;
	gint	 num_decimals;	/* 0 - 30 */
	gint	 negative_fmt;	/* 0 - 3 */
} FormatCharacteristics;

FormatFamily cell_format_classify (char const * const fmt, FormatCharacteristics *info);

extern const char *cell_format_general [];
extern const char *cell_format_numbers [];
extern const char *cell_format_accounting [];
extern const char *cell_format_date [];
extern const char *cell_format_hour [];
extern const char *cell_format_percent [];
extern const char *cell_format_fraction [];
extern const char *cell_format_scientific [];
extern const char *cell_format_text [];
extern const char *cell_format_money [];

#endif
