#ifndef GNUMERIC_PARSE_UTIL_H
# define GNUMERIC_PARSE_UTIL_H

#include "gnumeric.h"

char const  *col_name                 (int col);
char const  *cols_name                (int start_col, int end_col);
char const  *row_name                 (int row);
char const  *rows_name                (int start_row, int end_col);

char        *cellref_name            (CellRef const *cell_ref,
				      ParsePos const *pp, gboolean no_sheetname);
char const  *cellref_get             (CellRef *out, char const *in,
				      CellPos const *pos);
char const  *cellref_a1_get          (CellRef *out, char const *in,
				      CellPos const *pos);
char const  *cellref_r1c1_get        (CellRef *out, char const *in,
				      CellPos const *pos);

char const *cell_coord_name	     (int col, int row);
char const *cell_pos_name	     (CellPos const *pos);
char const *cell_name                (Cell const *cell);

/* Various parsing routines */
int         parse_col_name           (char const *cell_str, char const **endptr);
gboolean    parse_cell_name          (char const *cell_str, int *col, int *row,
				      gboolean strict, int *chars_read);
gboolean    parse_cell_range         (Sheet *sheet, char const *range, Value **v);

void	    parse_text_value_or_expr (ParsePos const *pos,
				      char const *text,
				      Value **val, GnmExpr const **expr,
				      StyleFormat *current_format /* can be NULL */);
gboolean   parse_surrounding_ranges  (char const *text, gint cursor, Sheet *sheet,
				      gboolean single_range_only, gint *from, gint *to,
				      RangeRef **range);

/* Is this string potentially the start of an expression */
char const * gnumeric_char_start_expr_p (char const * c);

typedef enum {
	PERR_NONE,
	PERR_MISSING_PAREN_OPEN,
	PERR_MISSING_PAREN_CLOSE,
	PERR_MISSING_CLOSING_QUOTE,
	PERR_INVALID_EXPRESSION,
	PERR_INVALID_ARRAY_SEPARATOR,
	PERR_UNKNOWN_SHEET,
	PERR_UNKNOWN_EXPRESSION,
	PERR_UNEXPECTED_TOKEN,
	PERR_OUT_OF_RANGE,
	PERR_SHEET_IS_REQUIRED,
	PERR_SINGLE_RANGE,
	PERR_MULTIPLE_EXPRESSIONS
} ParseErrorID;

/* In parser.y  */
struct _ParseError {
	ParseErrorID  id;
	char         *message;
	int           begin_char, end_char;
};

ParseError *parse_error_init (ParseError *pe);
void        parse_error_free (ParseError *pe);

typedef enum {
	GNM_EXPR_PARSE_DEFAULT = 0, /* default is Excel */
	GNM_EXPR_PARSE_USE_APPLIX_REFERENCE_CONVENTIONS	= 1 << 0,
	GNM_EXPR_PARSE_CREATE_PLACEHOLDER_FOR_UNKNOWN_FUNC	= 1 << 1,
	GNM_EXPR_PARSE_FORCE_ABSOLUTE_COL_REFERENCES	= 1 << 2,
	GNM_EXPR_PARSE_FORCE_ABSOLUTE_ROW_REFERENCES	= 1 << 3,
	GNM_EXPR_PARSE_FORCE_EXPLICIT_SHEET_REFERENCES	= 1 << 4,
	GNM_EXPR_PARSE_PERMIT_MULTIPLE_EXPRESSIONS		= 1 << 5
} GnmExprParseFlags;
#define gnm_expr_parse_str_simple(expr_text, pp) \
	gnm_expr_parse_str (expr_text, pp, GNM_EXPR_PARSE_DEFAULT, NULL)
GnmExpr const *gnm_expr_parse_str (char const *expr, ParsePos const *pp,
				   GnmExprParseFlags flags,
				   ParseError *error);
#endif /* GNUMERIC_PARSE_UTIL_H */
