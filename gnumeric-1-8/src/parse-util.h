/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_PARSE_UTIL_H_
# define _GNM_PARSE_UTIL_H_

#include "gnumeric.h"
#include "libgnumeric.h"

G_BEGIN_DECLS

char const *col_name  (int col);
char const *cols_name (int start_col, int end_col);
char const *col_parse (char const *str, int *res, unsigned char *relative);

char const *row_name  (int row);
char const *rows_name (int start_row, int end_col);
char const *row_parse (char const *str, int *res, unsigned char *relative);

char const *cellpos_as_string	(GnmCellPos const *pos);
char const *cellpos_parse	(char const *cell_str, GnmCellPos *res,
				 gboolean strict);
void        cellref_as_string   (GnmConventionsOut *out,
				 GnmCellRef const *cell_ref,
				 gboolean no_sheetname);
char const *cellref_parse	(GnmCellRef *out, char const *in,
				 GnmCellPos const *pos);

void        rangeref_as_string  (GnmConventionsOut *out,
				 GnmRangeRef const *ref);
char const *rangeref_parse	(GnmRangeRef *res, char const *in,
				 GnmParsePos const *pp,
				 GnmConventions const *convs);
				 /* GError **err); */

char const *sheetref_parse	(char const *start, Sheet **sheet,
				 Workbook const *wb, gboolean allow_3d);

char const *cell_coord_name	(int col, int row);
char const *cell_name		(GnmCell const *cell);

char const *parsepos_as_string	(GnmParsePos const *pp);

/* backwards compatibility version */
void gnm_1_0_rangeref_as_string (GnmConventionsOut *out,
				 GnmRangeRef const *ref);


struct _GnmConventionsOut {
	GString	*accum;
	GnmParsePos const *pp;
	GnmConventions const *convs;
};

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
	GNM_EXPR_PARSE_FORCE_ABSOLUTE_REFERENCES	   = 1 << 0,
	GNM_EXPR_PARSE_FORCE_RELATIVE_REFERENCES	   = 1 << 0,
	GNM_EXPR_PARSE_FORCE_EXPLICIT_SHEET_REFERENCES	   = 1 << 2,
	GNM_EXPR_PARSE_PERMIT_MULTIPLE_EXPRESSIONS	   = 1 << 3,
	GNM_EXPR_PARSE_UNKNOWN_NAMES_ARE_STRINGS	   = 1 << 4,
	GNM_EXPR_PARSE_UNKNOWN_NAMES_ARE_INVALID	   = 1 << 5
} GnmExprParseFlags;

struct _GnmConventions {
#if 0
	/* Not yet.  */
	gboolean force_absolute_col_references;
	gboolean force_absolute_row_references;
	gboolean force_explicit_sheet_references;
#endif
	gboolean r1c1_addresses;

	/* Separate elements in lists, 0 will use go_locale. */
	gunichar arg_sep;
	/* Separate array columns, 0 will use go_locale. */
	gunichar array_col_sep;
	/* Separate array rows, 0 will use go_locale.  */
	gunichar array_row_sep;

	/* What character denotes range intersection?  */
	gunichar intersection_char;

	/* What characters are range separators?  */
	gboolean range_sep_colon;  /* A1:B2 */
	gboolean range_sep_dotdot; /* A1..B2 */

	/* Separates sheet name from the cell ref */
	gunichar sheet_name_sep;

	/* Formerly USE_APPLIX_CONVENTIONS.  */
	gboolean ignore_whitespace;

	/* Formerly more or less part of USE_APPLIX_CONVENTIONS.  */
	gboolean allow_absolute_sheet_references;

	/* Formerly part of USE_OPENCALC_CONVENTIONS.  */
	gboolean decode_ampersands;

	/* Is the decimal separator "." (as opposed to locale's)?  */
	gboolean decimal_sep_dot;

	/* Accept prefix #NOT# and infixs #AND# and #OR#.  */
	gboolean accept_hash_logicals;

/* Import specific functions ------------------------------------- */
	struct {
		/* Called a lot for anything that might be a reference.  */
		char const *(*range_ref) (GnmRangeRef *res, char const *in,
					  GnmParsePos const *pp,
					  GnmConventions const *convs);
					/* GError **err); */

		/* Called a lot for anything that might be a function name or
		 * defined name.  */
		char const *(*name) (char const *in,
				     GnmConventions const *convs);


		/* Must return non-NULL, and absorb the args, including the list. */
		GnmExpr const *(*func) (GnmConventions const *convs,
				        /* make scope more useful, eg a
					 * ParsePos * to allow for
					 * sheet/object specific functions
					 * */
					Workbook *scope,
					char const *name,
					GnmExprList *args);
	} input;

/* Export specific functions ----------------------------------- */
	struct {
		gboolean translated;

		void (*string)	  (GnmConventionsOut *out,
				   GnmString const *str);
		void (*name)	  (GnmConventionsOut *out,
				   GnmExprName const *name);
		void (*cell_ref)  (GnmConventionsOut *out,
				   GnmCellRef const *cell_ref,
				   gboolean no_sheetname);
		void (*range_ref) (GnmConventionsOut *out,
				   GnmRangeRef const *range_ref);

		GString * (*quote_sheet_name) (GnmConventions const *convs,
					       char const *name);
	} output;
};
GnmConventions *gnm_conventions_new	 (void);
GnmConventions *gnm_conventions_new_full (unsigned size);
void		gnm_conventions_free	 (GnmConventions *c);

GNM_VAR_DECL GnmConventions const *gnm_conventions_default;
GNM_VAR_DECL GnmConventions const *gnm_conventions_xls_r1c1;

/**********************************************/

void parse_util_init (void);
void parse_util_shutdown (void);

GnmExprTop const *gnm_expr_parse_str (char const *str, GnmParsePos const *pp,
				      GnmExprParseFlags flags,
				      GnmConventions const *convs,
				      GnmParseError *error);

GnmExprTop const *gnm_expr_parse_str_simple (char const *str,
					     GnmParsePos const *pp);

/* Is this string potentially the start of an expression */
char const *gnm_expr_char_start_p (char const *c);

void	    parse_text_value_or_expr (GnmParsePos const *pos,
				      char const *text,
				      GnmValue **val,
				      GnmExprTop const **texpr,
				      GOFormat *current_format,
				      GODateConventions const *date_conv);

GString	*gnm_expr_conv_quote (GnmConventions const *conv, char const *str);

G_END_DECLS

#endif /* _GNM_PARSE_UTIL_H_ */
