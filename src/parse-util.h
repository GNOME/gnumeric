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
gboolean    parse_cell_name_or_range (char const *cell_str, int *col, int *row,
				      int *cols, int *rows, gboolean strict);
gboolean    parse_cell_range         (Sheet *sheet, char const *range, Value **v);
GSList     *parse_cell_name_list     (Sheet *sheet, char const *cell_name_str,
				      int *error_flag, gboolean strict);

StyleFormat *parse_text_value_or_expr (ParsePos const *pos,
				       char const *text,
				       Value **val, ExprTree **expr,
				       StyleFormat *current_format /* can be NULL */);

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
} ParseErrorID;

/* In parser.y  */
typedef struct {
	ParseErrorID  id;
	char         *message;
	int           begin_char, end_char;
} ParseError;
ParseError *parse_error_init (ParseError *pe);
void        parse_error_free (ParseError *pe);

ExprTree *gnumeric_expr_parser (char const *expr_text, ParsePos const *pos,
				gboolean use_excel_range_conventions,
				gboolean create_place_holder_for_unknown_func,
				StyleFormat **desired_format,
				ParseError *pe);

#endif /* GNUMERIC_PARSE_UTIL_H */
