#ifndef GNUMERIC_PARSE_UTIL_H
# define GNUMERIC_PARSE_UTIL_H

#include "gnumeric.h"

/*
 * Names
 */
const char *col_name                 (int col);
int         col_from_name            (const char *cell_str);

char        *cellref_name            (CellRef *cell_ref,
				      ParsePosition const *pp);
gboolean     cellref_get             (CellRef *out, const char *in,
				      int parse_col, int parse_row);
gboolean     cellref_a1_get          (CellRef *out, const char *in,
				      int parse_col, int parse_row);
gboolean     cellref_r1c1_get        (CellRef *out, const char *in,
				      int parse_col, int parse_row);

const char *cell_coord_name	     (int const col, int const row);
const char *cell_pos_name	     (CellPos const *pos);
const char *cell_name                (Cell const *cell);

/* Various parsing routines */
gboolean    parse_cell_name          (const char *cell_str, int *col, int *row,
				      gboolean strict, int *chars_read);
gboolean    parse_cell_name_or_range (const char *cell_str, int *col, int *row,
				      int *cols, int *rows, gboolean strict);
gboolean    parse_cell_range         (Sheet *sheet, const char *range, Value **v);
GSList     *parse_cell_name_list     (Sheet *sheet, const char *cell_name_str,
				      int *error_flag, gboolean strict);

char const *parse_text_value_or_expr (EvalPosition const * pos,
				      char const * const text,
				      Value **val, ExprTree **expr);

/* Is this string potentially the start of an expression */
char const * gnumeric_char_start_expr_p (char const * c);

/* In parser.y  */
typedef enum {
	PARSE_OK,
	PARSE_ERR_NO_QUOTE,
	PARSE_ERR_SYNTAX,
	PARSE_ERR_UNKNOWN
} ParseErr;
ParseErr    gnumeric_expr_parser   (const char *expr,
				    const ParsePosition *pp,
				    gboolean use_excel_range_conventions,
				    char **desired_format,
				    ExprTree **result);

#endif /* GNUMERIC_PARSE_UTIL_H */
