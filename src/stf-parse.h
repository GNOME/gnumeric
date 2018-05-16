#ifndef _GNM_STF_PARSE_H_
# define _GNM_STF_PARSE_H_

#include <glib.h>
#include <gnumeric.h>

G_BEGIN_DECLS

typedef enum {
	PARSE_TYPE_NOTSET    = 1 << 0,
	PARSE_TYPE_CSV       = 1 << 1,
	PARSE_TYPE_FIXED     = 1 << 2
} StfParseType_t;

/* Additive.  */
typedef enum {
	TRIM_TYPE_NEVER      = 0,
	TRIM_TYPE_LEFT       = 1 << 0,
	TRIM_TYPE_RIGHT      = 1 << 1
} StfTrimType_t;

typedef struct {
	StfParseType_t       parsetype;             /* The type of import to do */
	StfTrimType_t        trim_spaces;           /* Trim spaces in fields? */

	GSList *             terminator;            /* Line terminators */
	char *               locale;

	struct _StfCompiledTerminator {
		guchar       min, max;
	} compiled_terminator;

	/* CSV related */
	struct _StfSeparator {
		GSList *str;
		char   *chr;
		gboolean duplicates;         /* See two text separators as one? */
	} sep;
	gunichar             stringindicator;       /* String indicator */
	gboolean             indicator_2x_is_single;/* 2 quote chars form a single non-terminating quote */
	gboolean             trim_seps;             /* Ignore initial seps.  */

	/* Fixed width related */
	GArray              *splitpositions;        /* Positions where text will be split vertically */

        gboolean             *col_autofit_array;    /* 0/1 array indicating  */
	                                            /* which col widths to autofit  */
        gboolean             *col_import_array;     /* 0/1 array indicating  */
	                                            /* which cols to import  */
	unsigned int         col_import_array_len;

	GPtrArray            *formats;              /* Contains GOFormat *s */
	GPtrArray            *formats_decimal;      /* Contains GString *s */
	GPtrArray            *formats_thousand;     /* Contains GString *s */
	GPtrArray            *formats_curr;         /* Contains GString *s */

	gboolean             cols_exceeded;         /* This is set to TRUE if */
	                                            /* we tried to import more than */
	                                            /* SHEET_MAX_COLS columns */
	gboolean             rows_exceeded;         /* Ditto rows.  */
	unsigned             ref_count;             /* Boxed type */
} StfParseOptions_t;

/* CREATION/DESTRUCTION of stf options struct */

GType               stf_parse_options_get_type                        (void);
void                stf_parse_options_free                            (StfParseOptions_t *parseoptions);

StfParseOptions_t  *stf_parse_options_guess                           (char const *data);
StfParseOptions_t  *stf_parse_options_guess_csv                       (char const *data);
void                stf_parse_options_guess_formats                   (StfParseOptions_t *po,
								       char const *data);

/* MANIPULATION of stf options struct */

void stf_parse_options_set_type                        (StfParseOptions_t *parseoptions,
							StfParseType_t const parsetype);
void stf_parse_options_clear_line_terminator           (StfParseOptions_t *parseoptions);
void stf_parse_options_add_line_terminator             (StfParseOptions_t *parseoptions,
							char const *terminator);
void stf_parse_options_set_trim_spaces                 (StfParseOptions_t *parseoptions,
							StfTrimType_t const trim_spaces);
void stf_parse_options_csv_set_separators              (StfParseOptions_t *parseoptions,
							char const *character, GSList const *seps);
void stf_parse_options_csv_set_stringindicator         (StfParseOptions_t *parseoptions,
							gunichar const stringindicator);
void stf_parse_options_csv_set_indicator_2x_is_single  (StfParseOptions_t *parseoptions,
							gboolean const indic_2x);
void stf_parse_options_csv_set_duplicates              (StfParseOptions_t *parseoptions,
							gboolean const duplicates);
void stf_parse_options_csv_set_trim_seps               (StfParseOptions_t *parseoptions,
							gboolean const trim_seps);
void stf_parse_options_fixed_splitpositions_clear      (StfParseOptions_t *parseoptions);
void stf_parse_options_fixed_splitpositions_add        (StfParseOptions_t *parseoptions,
							int position);
void stf_parse_options_fixed_splitpositions_remove     (StfParseOptions_t *parseoptions,
							int position);
int stf_parse_options_fixed_splitpositions_count       (StfParseOptions_t *parseoptions);
int stf_parse_options_fixed_splitpositions_nth         (StfParseOptions_t *parseoptions, int n);

/* USING the stf structs to actually do some parsing, these are the lower-level functions and utility functions */

GPtrArray	*stf_parse_general			(StfParseOptions_t *parseoptions,
							 GStringChunk *lines_chunk,
							 char const *data,
							 char const *data_end);
void		 stf_parse_general_free			(GPtrArray *lines);
GPtrArray	*stf_parse_lines			(StfParseOptions_t *parseoptions,
							 GStringChunk *lines_chunk,
							 char const *data,
							 int maxlines,
							 gboolean with_lineno);

void		 stf_parse_options_fixed_autodiscover	(StfParseOptions_t *parseoptions,
							 char const *data,
							 char const *data_end);

char const	*stf_parse_find_line			(StfParseOptions_t *parseoptions,
							 char const *data,
							 int line);

/* Higher level functions, can be used for directly parsing into an application specific data container */
gboolean	 stf_parse_sheet			(StfParseOptions_t *parseoptions,
							 char const *data, char const *data_end,
							 Sheet *sheet,
							 int start_col, int start_row);

GnmCellRegion	*stf_parse_region			(StfParseOptions_t *parseoptions,
							 char const *data, char const *data_end,
							 Workbook const *wb);

G_END_DECLS

#endif /* _GNM_STF_PARSE_H_ */
