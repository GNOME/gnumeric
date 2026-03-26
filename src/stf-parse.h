#ifndef GNM_STF_PARSE_H_
#define GNM_STF_PARSE_H_

#include <glib.h>
#include <gnumeric.h>

G_BEGIN_DECLS

GType gnm_stf_parsed_lines_get_type (void);
#define GNM_STF_PARSED_LINES_TYPE (gnm_stf_parsed_lines_get_type ())
#define GNM_STF_PARSED_LINES(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_STF_PARSED_LINES_TYPE, GnmStfParsedLines))
#define GNM_IS_STF_PARSED_LINES(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_STF_PARSED_LINES_TYPE))

struct GnmStfParsedLines_ {
	GObject parent;

	GStringChunk *lines_chunk;
	GPtrArray *lines;
};




typedef enum {
	PARSE_TYPE_NOTSET    = 1 << 0,
	PARSE_TYPE_CSV       = 1 << 1,
	PARSE_TYPE_FIXED     = 1 << 2
} GnmStfParseType;

/* Additive.  */
typedef enum {
	TRIM_TYPE_NEVER      = 0,
	TRIM_TYPE_LEFT       = 1 << 0,
	TRIM_TYPE_RIGHT      = 1 << 1
} GnmStfTrimType;



GType               stf_parse_options_get_type                        (void);
#define GNM_STF_PARSE_OPTIONS_TYPE (stf_parse_options_get_type ())
#define GNM_STF_PARSE_OPTIONS(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_STF_PARSE_OPTIONS_TYPE, GnmStfParseOptions))
#define GNM_IS_STF_PARSE_OPTIONS(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_STF_PARSE_OPTIONS_TYPE))

struct GnmStfParseOptions_ {
	GObject              parent;

	GnmStfParseType       parsetype;             /* The type of import to do */
	GnmStfTrimType        trim_spaces;           /* Trim spaces in fields? */

	GSList *             terminator;            /* Line terminators */
	char *               locale;

	struct {
		guchar       min, max;
	} compiled_terminator;

	/* CSV related */
	struct {
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
};

/* CREATION/DESTRUCTION of stf options struct */

GnmStfParseOptions  *stf_parse_options_guess                           (char const *data);
GnmStfParseOptions  *stf_parse_options_guess_csv                       (char const *data);
void                stf_parse_options_guess_formats                   (GnmStfParseOptions *po,
								       char const *data);

/* MANIPULATION of stf options struct */

void stf_parse_options_set_type                        (GnmStfParseOptions *parseoptions,
							GnmStfParseType parsetype);
void stf_parse_options_clear_line_terminator           (GnmStfParseOptions *parseoptions);
void stf_parse_options_add_line_terminator             (GnmStfParseOptions *parseoptions,
							char const *terminator);
void stf_parse_options_set_trim_spaces                 (GnmStfParseOptions *parseoptions,
							GnmStfTrimType const trim_spaces);
void stf_parse_options_csv_set_separators              (GnmStfParseOptions *parseoptions,
							char const *character, GSList const *seps);
void stf_parse_options_csv_set_stringindicator         (GnmStfParseOptions *parseoptions,
							gunichar stringindicator);
void stf_parse_options_csv_set_indicator_2x_is_single  (GnmStfParseOptions *parseoptions,
							gboolean indic_2x);
void stf_parse_options_csv_set_duplicates              (GnmStfParseOptions *parseoptions,
							gboolean duplicates);
void stf_parse_options_csv_set_trim_seps               (GnmStfParseOptions *parseoptions,
							gboolean trim_seps);
void stf_parse_options_fixed_splitpositions_clear      (GnmStfParseOptions *parseoptions);
void stf_parse_options_fixed_splitpositions_add        (GnmStfParseOptions *parseoptions,
							int position);
void stf_parse_options_fixed_splitpositions_remove     (GnmStfParseOptions *parseoptions,
							int position);
int stf_parse_options_fixed_splitpositions_count       (GnmStfParseOptions *parseoptions);
int stf_parse_options_fixed_splitpositions_nth         (GnmStfParseOptions *parseoptions, int n);

/* USING the stf structs to actually do some parsing, these are the lower-level functions and utility functions */

GnmStfParsedLines *stf_parse_general			(GnmStfParseOptions *parseoptions,
							 char const *data,
							 char const *data_end);
GnmStfParsedLines *stf_parse_lines			(GnmStfParseOptions *parseoptions,
							 char const *data,
							 int maxlines,
							 gboolean with_lineno);

void		 stf_parse_options_fixed_autodiscover	(GnmStfParseOptions *parseoptions,
							 char const *data,
							 char const *data_end);

char const	*stf_parse_find_line			(GnmStfParseOptions *parseoptions,
							 char const *data,
							 int line);

/* Higher level functions, can be used for directly parsing into an application specific data container */
gboolean	 stf_parse_sheet			(GnmStfParseOptions *parseoptions,
							 char const *data, char const *data_end,
							 Sheet *sheet,
							 int start_col, int start_row);

GnmCellRegion	*stf_parse_region			(GnmStfParseOptions *parseoptions,
							 char const *data, char const *data_end,
							 Workbook const *wb);

G_END_DECLS

#endif /* GNM_STF_PARSE_H_ */
