#ifndef GNUMERIC_PARSE_UTIL_H
# define GNUMERIC_PARSE_UTIL_H

#include "gnumeric.h"

char const *col_name  (int col);
char const *cols_name (int start_col, int end_col);
char const *col_parse (char const *str, int *res, unsigned char *relative);

char const *row_name  (int row);
char const *rows_name (int start_row, int end_col);
char const *row_parse (char const *str, int *res, unsigned char *relative);

char const *cellpos_as_string	(CellPos const *pos);
gboolean    cellpos_parse	(char const *cell_str, CellPos *res,
				 gboolean strict, int *chars_read);

char       *cellref_as_string	(CellRef const *ref, ParsePos const *pp,
				 gboolean no_sheetname);
char const *cellref_parse	(CellRef *out, char const *in,
				 CellPos const *pos);

char	   *rangeref_as_string	(RangeRef const *ref, ParsePos const *pp);
char const *rangeref_parse	(RangeRef *res, char const *in,
				 ParsePos const *pp);
				 //GError **err);

char const *sheetref_parse	(char const *start, Sheet **sheet,
				 Workbook const *wb, gboolean allow_3d);

char const *cell_coord_name	(int col, int row);
char const *cell_name		(Cell const *cell);

/* backwards compatibility versions that will move to a plugin */
char	   *gnm_1_0_rangeref_as_string	(RangeRef const *ref, ParsePos const *pp);
char const *gnm_1_0_rangeref_parse	(RangeRef *res, char const *in,
					 ParsePos const *pp);

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
	PERR_SET_CONTENT_MUST_BE_RANGE
} ParseErrorID;

/* In parser.y  */
struct _ParseError {
	GError	*err;
	int begin_char, end_char;
};

ParseError *parse_error_init (ParseError *pe);
void        parse_error_free (ParseError *pe);

typedef enum {
	GNM_EXPR_PARSE_DEFAULT = 0, /* default is Excel */
	GNM_EXPR_PARSE_USE_APPLIX_CONVENTIONS	   	   = 1 << 0,
	GNM_EXPR_PARSE_USE_OPENCALC_CONVENTIONS		   = 1 << 1,
	GNM_EXPR_PARSE_CREATE_PLACEHOLDER_FOR_UNKNOWN_FUNC = 1 << 2,
	GNM_EXPR_PARSE_FORCE_ABSOLUTE_COL_REFERENCES	   = 1 << 3,
	GNM_EXPR_PARSE_FORCE_ABSOLUTE_ROW_REFERENCES	   = 1 << 4,
	GNM_EXPR_PARSE_FORCE_EXPLICIT_SHEET_REFERENCES	   = 1 << 5,
	GNM_EXPR_PARSE_PERMIT_MULTIPLE_EXPRESSIONS	   = 1 << 6,
	GNM_EXPR_PARSE_UNKNOWN_NAMES_ARE_STRINGS	   = 1 << 7
} GnmExprParseFlags;

typedef char const *(*GnmRangeRefParse) (RangeRef *res, char const *in,
					 ParsePos const *pp);
					 //GError **err);

#define gnm_expr_parse_str_simple(expr_text, pp) \
	gnm_expr_parse_str (expr_text, pp, GNM_EXPR_PARSE_DEFAULT, &rangeref_parse, NULL)
GnmExpr const *gnm_expr_parse_str (char const *expr, ParsePos const *pp,
				   GnmExprParseFlags flags,
				   GnmRangeRefParse ref_parser,
				   ParseError *error);

/* Is this string potentially the start of an expression */
char const *gnm_expr_char_start_p (char const *c);

void	    parse_text_value_or_expr (ParsePos const *pos,
				      char const *text,
				      Value **val, GnmExpr const **expr,
				      StyleFormat *current_format /* can be NULL */);


#endif /* GNUMERIC_PARSE_UTIL_H */
