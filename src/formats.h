/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#ifndef GNUMERIC_FORMATS_H
#define GNUMERIC_FORMATS_H

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
	FMT_SPECIAL	= 10
} FormatFamily;

typedef struct {
	unsigned char const * const symbol;
	unsigned char const * const description;
	gboolean const precedes;
	gboolean const has_space;
} CurrencySymbol;

typedef struct {
	gboolean thousands_sep;
	gint	 num_decimals;	/* 0 - 30 */
	gint	 negative_fmt;	/* 0 - 3 */
	gint	 currency_symbol_index;
	gint	 list_element;
	gboolean date_has_days;
	gboolean date_has_months;
} FormatCharacteristics;

FormatFamily cell_format_classify (StyleFormat const *fmt, FormatCharacteristics *info);

/* Indexed by FormatCharacteristics */
extern char const * const * const cell_formats [];

extern CurrencySymbol const currency_symbols [];

void currency_date_format_init     (void);
void currency_date_format_shutdown (void);

void style_format_percent (GString *result, FormatCharacteristics const *fmt);
void style_format_science (GString *result, FormatCharacteristics const *fmt);
void style_format_account (GString *result, FormatCharacteristics const *fmt);
void style_format_number  (GString *result, FormatCharacteristics const *fmt);

#endif
