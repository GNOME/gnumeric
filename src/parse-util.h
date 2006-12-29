#ifndef GNM_PARSE_UTIL_H
# define GNM_PARSE_UTIL_H

#include "gnumeric.h"

char const *col_name  (int col);
char const *cols_name (int start_col, int end_col);
char const *col_parse (char const *str, int *res, unsigned char *relative);

char const *row_name  (int row);
char const *rows_name (int start_row, int end_col);
char const *row_parse (char const *str, int *res, unsigned char *relative);

char const *cellpos_as_string	(GnmCellPos const *pos);
char const *cellpos_parse	(char const *cell_str, GnmCellPos *res,
				 gboolean strict);
void        cellref_as_string   (GString *target, GnmExprConventions const *convs,
				 GnmCellRef const *cell_ref,
				 GnmParsePos const *pp, gboolean no_sheetname);
char const *cellref_parse	(GnmCellRef *out, char const *in,
				 GnmCellPos const *pos);

void        rangeref_as_string (GString *target, GnmExprConventions const *convs,
				GnmRangeRef const *ref, GnmParsePos const *pp);
char const *rangeref_parse	(GnmRangeRef *res, char const *in,
				 GnmParsePos const *pp,
				 GnmExprConventions const *convs);
				 /* GError **err); */

char const *sheetref_parse	(char const *start, Sheet **sheet,
				 Workbook const *wb, gboolean allow_3d);

char const *cell_coord_name	(int col, int row);
char const *cell_name		(GnmCell const *cell);

/* backwards compatibility version */
void gnm_1_0_rangeref_as_string (GString *target, GnmExprConventions const *conv,
				 GnmRangeRef const *ref, GnmParsePos const *pp);

typedef enum {
	PERR_NONE,
	PERR_MISSING_PAREN_OPEN,
	PERR_MISSING_PAREN_CLOSE,
	PERR_MISSING_CLOSING_QUOTE,
	PERR_INVALID_EXPRESSION,
	PERR_INVALID_ARRAY_SEPARATOR,
	PERR_UNKNOWN_WORKBOOK,
	PERR_UNKNOWN_SHEET,
	PERR_UNKNOWN_NAME,
	PERR_UNEXPECTED_TOKEN,
	PERR_OUT_OF_RANGE,
	PERR_SHEET_IS_REQUIRED,
	PERR_SINGLE_RANGE,
	PERR_3D_NAME,
	PERR_MULTIPLE_EXPRESSIONS,
	PERR_INVALID_EMPTY,
	PERR_ASYMETRIC_ARRAY,
	PERR_SET_CONTENT_MUST_BE_RANGE
} ParseErrorID;

/* In parser.y  */
struct _GnmParseError {
	GError	*err;
	int begin_char, end_char;
};

GnmParseError *parse_error_init (GnmParseError *pe);
void        parse_error_free (GnmParseError *pe);

typedef enum {
	GNM_EXPR_PARSE_DEFAULT = 0, /* default is Excel */
	GNM_EXPR_PARSE_FORCE_ABSOLUTE_COL_REFERENCES	   = 1 << 0,
	GNM_EXPR_PARSE_FORCE_ABSOLUTE_ROW_REFERENCES	   = 1 << 1,
	GNM_EXPR_PARSE_FORCE_EXPLICIT_SHEET_REFERENCES	   = 1 << 2,
	GNM_EXPR_PARSE_PERMIT_MULTIPLE_EXPRESSIONS	   = 1 << 3,
	GNM_EXPR_PARSE_UNKNOWN_NAMES_ARE_STRINGS	   = 1 << 4,
	GNM_EXPR_PARSE_UNKNOWN_NAMES_ARE_INVALID	   = 1 << 5
} GnmExprParseFlags;

typedef char const *(*GnmRangeRefParse) (GnmRangeRef *res, char const *in,
					 GnmParsePos const *pp,
					 GnmExprConventions const *convs);
					 /* GError **err); */
typedef char const *(*GnmNameParse) (char const *in,
				     GnmExprConventions const *convs);

/*
 * On success, this functions should return a non-NULL value and
 * absorb the args, including the list.
 */
typedef GnmExpr const *(*GnmParseFunctionHandler) (char const *name,
						   GnmExprList *args,
						   GnmExprConventions const *convs);

typedef void (*GnmParseExprNameHandler) (GString *target,
					 GnmParsePos const *pp,
					 GnmExprName const *name,
					 GnmExprConventions const *convs);

typedef void (*GnmParseCellRefHandler) (GString *target,
					GnmExprConventions const *convs,
					GnmCellRef const *cell_ref,
					GnmParsePos const *pp,
					gboolean no_sheetname);

typedef void (*GnmParseRangeRefHandler) (GString *target,
					 GnmExprConventions const *convs,
					 GnmRangeRef const *cell_ref,
					 GnmParsePos const *pp);

typedef GString * (*GnmSheetNameQuoteHandler) (GnmExprConventions const *convs,
					       char const *name);

struct _GnmExprConventions {
#if 0
	/* Not yet.  */
	gboolean force_absolute_col_references;
	gboolean force_absolute_row_references;
	gboolean force_explicit_sheet_references;
#endif
	gboolean r1c1_addresses;

	/* What character denotes range intersection?  */
	gunichar intersection_char;

	/* What characters are range separators?  */
	gboolean range_sep_colon;  /* A1:B2 */
	gboolean range_sep_dotdot; /* A1..B2 */

	/* What characters are sheet separators?  */
	gboolean sheet_sep_exclamation;  /* Sheet!... */
	gboolean sheet_sep_colon; /* Sheet:... */

	/* Formerly USE_APPLIX_CONVENTIONS.  */
	gboolean ignore_whitespace;

	/* Formerly more or less part of USE_APPLIX_CONVENTIONS.  */
	gboolean allow_absolute_sheet_references;

	/* Formerly part of USE_OPENCALC_CONVENTIONS.  */
	gboolean decode_ampersands;

	/* Is the decimal separator "." (as opposed to locale's)?  */
	gboolean decimal_sep_dot;

	/* Is the argument separator ";" (as opposed to locale's)?  */
	gboolean argument_sep_semicolon;

	/* Is the array column separator "," (as opposed to locale's)?  */
	gboolean array_col_sep_comma;

	/* Accept prefix #NOT# and infixs #AND# and #OR#.  */
	gboolean accept_hash_logicals;

	/* Called a lot for anything that might be a reference.  */
	GnmRangeRefParse ref_parser;

	/* Called a lot for anything that might be a function name or defined
	   name.  */
	GnmNameParse name_parser;

	/*
	 * Optional name->GnmParseFunctionHandler hash.  When a name is
	 * present in this hash, unknown_function_handler will not be
	 * called even if the name is unknown.
	 */
	GHashTable *function_rewriter_hash;

	/* Called for unknown functions if non-NULL.  */
	GnmParseFunctionHandler unknown_function_handler;

	/* ----------------------------------------------------------------- */

	/* Called to make strings of names.  */
	GnmParseExprNameHandler expr_name_handler;

	/* Called to make strings of cell refs.  */
	GnmParseCellRefHandler cell_ref_handler;

	/* Called to make strings of range refs.  */
	GnmParseRangeRefHandler range_ref_handler;

	/* Called to optionally quote a sheet name.  */
	GnmSheetNameQuoteHandler sheet_name_quote;

	/* Used to separate sheet from name when both are needed.  */
	char const *output_sheet_name_sep;

	/* If non-null, used to separate elements in lists. */
	char const *output_argument_sep;

	/* If non-null, used to separate array columns. */
	char const *output_array_col_sep;

	/* If non-null, used to separate array columns.  */
	char const *output_array_row_sep;

	gboolean output_translated;
};

GnmExprConventions *gnm_expr_conventions_new	  (void);
GnmExprConventions *gnm_expr_conventions_new_full (unsigned size);
void gnm_expr_conventions_free (GnmExprConventions *c);

extern GnmExprConventions const *gnm_expr_conventions_default;
extern GnmExprConventions const *gnm_expr_conventions_r1c1;
void parse_util_init (void);
void parse_util_shutdown (void);

GnmExprTop const *gnm_expr_parse_str (char const *expr, GnmParsePos const *pp,
				      GnmExprParseFlags flags,
				      GnmExprConventions const *convs,
				      GnmParseError *error);

GnmExprTop const *gnm_expr_parse_str_simple (char const *expr,
					     GnmParsePos const *pp);

/* Is this string potentially the start of an expression */
char const *gnm_expr_char_start_p (char const *c);

void	    parse_text_value_or_expr (GnmParsePos const *pos,
				      char const *text,
				      GnmValue **val,
				      GnmExprTop const **texpr,
				      GOFormat *current_format,
				      GODateConventions const *date_conv);

GString	*gnm_expr_conv_quote (GnmExprConventions const *conv, char const *str);

#endif /* GNM_PARSE_UTIL_H */
