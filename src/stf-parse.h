#ifndef GNUMERIC_STF_PARSE_H
#define GNUMERIC_STF_PARSE_H

#include <stdlib.h>
#include <string.h>
#include <glib.h>

#include "sheet.h"
#include "cell.h"

typedef enum {
	STF_TOKEN_UNDEF = 0,
	STF_TOKEN_CHAR,
	STF_TOKEN_STRING,
	STF_TOKEN_STRING_INC
} StfTokenType_t;

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
     GSList *             terminator;            /* Line terminators */
     int                  parselines;            /* Number of lines to parse */
     StfTrimType_t        trim_spaces;           /* Trim spaces in fields ? */
     
     /* CSV related */
     struct {
	  GSList *str;
	  char   *chr;
     } sep;
     char                 stringindicator;       /* String indicator */
     gboolean             indicator_2x_is_single;/* 2 quote chars are a single non-terminating quote */
     gboolean             duplicates;            /* See two text separator's as one? */
     
     /* Fixed width related */
     GArray              *splitpositions;        /* Positions where text will be split vertically */
     
     int                  rowcount;              /* Number of rows parsed */
     int                  colcount;              /* Number of columns parsed */
} StfParseOptions_t;

/* CREATION/DESTRUCTION of stf options struct */

StfParseOptions_t  *stf_parse_options_new                             (void);
void                stf_parse_options_free                            (StfParseOptions_t *parseoptions);

/* MANIPULATION of stf options struct */

void stf_parse_options_set_type                        (StfParseOptions_t *parseoptions,
								       StfParseType_t const parsetype);
void stf_parse_options_clear_line_terminator           (StfParseOptions_t *parseoptions);
void stf_parse_options_add_line_terminator             (StfParseOptions_t *parseoptions,
                                                                       char const *terminator);
void stf_parse_options_remove_line_terminator          (StfParseOptions_t *parseoptions,
                                                                       char const *terminator);
void stf_parse_options_set_lines_to_parse              (StfParseOptions_t *parseoptions,
								       int const lines);
void stf_parse_options_set_trim_spaces                 (StfParseOptions_t *parseoptions,
								       StfTrimType_t const trim_spaces);
void stf_parse_options_csv_set_separators              (StfParseOptions_t *parseoptions,
								       char const *character, GSList const *string);
void stf_parse_options_csv_set_stringindicator         (StfParseOptions_t *parseoptions,
								       char const stringindicator);
void stf_parse_options_csv_set_indicator_2x_is_single  (StfParseOptions_t *parseoptions,
								       gboolean const indic_2x);
void stf_parse_options_csv_set_duplicates              (StfParseOptions_t *parseoptions,
								       gboolean const duplicates);
void stf_parse_options_fixed_splitpositions_clear      (StfParseOptions_t *parseoptions);
void stf_parse_options_fixed_splitpositions_add        (StfParseOptions_t *parseoptions,
								       int const position);

/* USING the stf structs to actually do some parsing, these are the lower-level functions and utility functions */

GList              *stf_parse_general                                 (StfParseOptions_t *parseoptions,
								       char const *data);

int                 stf_parse_get_rowcount                            (StfParseOptions_t *parseoptions,
								       char const *data);
int                 stf_parse_get_colcount                            (StfParseOptions_t *parseoptions,
								       char const *data);

int                 stf_parse_get_longest_row_width                   (StfParseOptions_t *parseoptions,
								       const char *data);

int                 stf_parse_get_colwidth                            (StfParseOptions_t *parseoptions,
								       const char *data, int const index);

int	            stf_parse_convert_to_unix                         (char *data);
char const         *stf_parse_is_valid_data                           (char const *data, int len);

void                stf_parse_options_fixed_autodiscover              (StfParseOptions_t *parseoptions,
								       int const data_lines, char const *data);

char               *stf_parse_next_token                              (char const *data, gunichar quote, 
								       gboolean adj_escaped, StfTokenType_t *tokentype);

/* Higher level functions, can be used for directly parsing into an application specific data container */
gboolean	    stf_parse_sheet                                   (StfParseOptions_t *parseoptions,
								       char const *data, Sheet *sheet,
								       int start_col, int start_row);

CellRegion         *stf_parse_region                                  (StfParseOptions_t *parseoptions,
								       char const *data);
#endif
