#ifndef GNUMERIC_PARSE_UTIL_H
# define GNUMERIC_PARSE_UTIL_H

#include "gnumeric.h"

/*
 * Names
 */
const char *col_name                 (int col);

char        *cellref_name            (CellRef const *cell_ref,
				      ParsePos const *pp, gboolean no_sheetname);
gboolean     cellref_get             (CellRef *out, const char *in,
				      CellPos const *pos);
gboolean     cellref_a1_get          (CellRef *out, const char *in,
				      CellPos const *pos);
gboolean     cellref_r1c1_get        (CellRef *out, const char *in,
				      CellPos const *pos);

const char *cell_coord_name	     (int const col, int const row);
const char *cell_pos_name	     (CellPos const *pos);
const char *cell_name                (Cell const *cell);

/* Various parsing routines */
int         parse_col_name           (const char *cell_str, const char **endptr);
gboolean    parse_cell_name          (const char *cell_str, int *col, int *row,
				      gboolean strict, int *chars_read);
gboolean    parse_cell_name_or_range (const char *cell_str, int *col, int *row,
				      int *cols, int *rows, gboolean strict);
gboolean    parse_cell_range         (Sheet *sheet, const char *range, Value **v);
GSList     *parse_cell_name_list     (Sheet *sheet, const char *cell_name_str,
				      int *error_flag, gboolean strict);

StyleFormat *parse_text_value_or_expr (EvalPos const *pos,
				       char const *text,
				       Value **val, ExprTree **expr,
				       StyleFormat const *current_format /* can be NULL */);

/* Is this string potentially the start of an expression */
char const * gnumeric_char_start_expr_p (char const * c);

/* In parser.y  */
typedef struct {
	char *message;
	int begin_char, end_char;
} ParseError;
ParseError *parse_error_init (ParseError *pe);
void        parse_error_free (ParseError *pe);

ExprTree *gnumeric_expr_parser (char const *expr_text, ParsePos const *pos,
				gboolean use_excel_range_conventions,
				gboolean create_place_holder_for_unknown_func,
				StyleFormat **desired_format,
				ParseError *pe);

#endif /* GNUMERIC_PARSE_UTIL_H */
