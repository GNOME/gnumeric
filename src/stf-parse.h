#ifndef GNUMERIC_STF_PARSE_H
#define GNUMERIC_STF_PARSE_H

#include <stdlib.h>
#include <gnome.h>

#include "sheet.h"
#include "cell.h"

typedef enum {
	TEXT_SEPARATOR_TAB       = 1 << 0,
	TEXT_SEPARATOR_COLON     = 1 << 1,
	TEXT_SEPARATOR_COMMA     = 1 << 2,
	TEXT_SEPARATOR_SPACE     = 1 << 3,
	TEXT_SEPARATOR_SEMICOLON = 1 << 4,
	TEXT_SEPARATOR_PIPE      = 1 << 5,
	TEXT_SEPARATOR_SLASH     = 1 << 6,
	TEXT_SEPARATOR_HYPHEN    = 1 << 7,
	TEXT_SEPARATOR_BANG      = 1 << 8,
	TEXT_SEPARATOR_CUSTOM    = 1 << 9
} StfTextSeparator_t;

typedef enum {
	PARSE_TYPE_NOTSET    = 1 << 0,
	PARSE_TYPE_CSV       = 1 << 1,
	PARSE_TYPE_FIXED     = 1 << 2
} StfParseType_t;

typedef enum {
	TRIM_TYPE_NEVER      = 1 << 0,
	TRIM_TYPE_LEFT       = 1 << 1,
	TRIM_TYPE_RIGHT      = 1 << 2
} StfTrimType_t;

typedef struct {
	StfParseType_t       parsetype;             /* The type of import to do */
	char                 terminator;            /* Line terminator */
	int                  parselines;            /* Number of lines to parse */
	StfTrimType_t        trim_spaces;           /* Trim spaces in fields ? */

	/* CSV related */
	StfTextSeparator_t   separators;            /* Text separator(s) */
	char                *customfieldseparator;  /* Custom text separator */
	char                 stringindicator;       /* String indicator */
	gboolean             indicator_2x_is_single;/* 2 quote chars are a single non-terminating quote */
	gboolean             duplicates;            /* See two text separator's as one? */

	/* Fixed width related */
        GArray              *splitpositions;        /* Positions where text will be split vertically */

	int                  rowcount;              /* Number of rows parsed */
	int                  colcount;              /* Number of columns parsed */

	/* Related to modification determination */
	GArray              *oldsplitpositions;     /* Splitpositions before before_modification was called */
	gboolean             modificationmode;      /* If TRUE we are in modification determination mode */
} StfParseOptions_t;

/* CREATION/DESTRUCTION of stf options struct */

StfParseOptions_t  *stf_parse_options_new                             (void);
void                stf_parse_options_free                            (StfParseOptions_t *parseoptions);

/* MANIPULATION of stf options struct */

void                stf_parse_options_set_type                        (StfParseOptions_t *parseoptions, StfParseType_t parsetype);
void                stf_parse_options_set_line_terminator             (StfParseOptions_t *parseoptions, char terminator);
void                stf_parse_options_set_lines_to_parse              (StfParseOptions_t *parseoptions, int lines);
void                stf_parse_options_set_trim_spaces                 (StfParseOptions_t *parseoptions, StfTrimType_t trim_spaces);

void                stf_parse_options_csv_set_separators              (StfParseOptions_t *parseoptions,
								       gboolean tab, gboolean colon,
								       gboolean comma, gboolean space,
								       gboolean semicolon, gboolean pipe,
								       gboolean slash, gboolean hyphen,
								       gboolean bang, gboolean custom);
void                stf_parse_options_csv_set_customfieldseparator    (StfParseOptions_t *parseoptions, const char *customfieldseparator);
void                stf_parse_options_csv_set_stringindicator         (StfParseOptions_t *parseoptions, char stringindicator);
void                stf_parse_options_csv_set_indicator_2x_is_single  (StfParseOptions_t *parseoptions, gboolean indic_2x);
void                stf_parse_options_csv_set_duplicates              (StfParseOptions_t *parseoptions, gboolean duplicates);

void                stf_parse_options_fixed_splitpositions_clear      (StfParseOptions_t *parseoptions);
void                stf_parse_options_fixed_splitpositions_add        (StfParseOptions_t *parseoptions, int position);

/* USING the stf structs to actually do some parsing, these are the lower-level functions and utility functions */

GSList             *stf_parse_general                                 (StfParseOptions_t *parseoptions, const char *data);

int                 stf_parse_get_rowcount                            (StfParseOptions_t *parseoptions, const char *data);
int                 stf_parse_get_colcount                            (StfParseOptions_t *parseoptions, const char *data);

int                 stf_parse_get_longest_row_width                   (StfParseOptions_t *parseoptions, const char *data);

int                 stf_parse_get_colwidth                            (StfParseOptions_t *parseoptions, const char *data, int index);

gboolean            stf_parse_convert_to_unix                         (char *data);
char               *stf_parse_is_valid_data                           (char *data);

void                stf_parse_options_fixed_autodiscover              (StfParseOptions_t *parseoptions, int data_lines, const char *data);

/* Higher level functions, can be used for directly parsing into an application specific data container */
Sheet              *stf_parse_sheet                                   (StfParseOptions_t *parseoptions, const char *data, Sheet *sheet);

CellRegion         *stf_parse_region                                  (StfParseOptions_t *parseoptions, const char *data);

#endif
