#ifndef GNUMERIC_CELL_H
#define GNUMERIC_CELL_H

typedef unsigned char  ColType;
typedef unsigned short RowType;

typedef struct {
	int        pos;			/* the column or row number */

	/* The height */
	int        units;		/* In units */
	int        margin_a;  		/* in pixels: top/left margin */
	int        margin_b; 		/* in pixels: bottom/right margin */
	int        pixels;		/* we compute this from the above parameters */

	unsigned   int selected:1;	/* is this selected? */
	unsigned   int hard_size:1;     /* has the user explicitly set the dimensions? */
	void       *data;
} ColRowInfo;

#define COL_INTERNAL_WIDTH(col) ((col)->pixels - ((col)->margin_b + (col)->margin_a))
#define ROW_INTERNAL_HEIGHT(row) ((row)->pixels - ((row)->margin_b + (row)->margin_a))


typedef struct {
	void       *sheet;
	ColRowInfo *col;
	ColRowInfo *row;

	/* Text as entered by the user */
	String    *entered_text;
	
	/* Type of the content and the actual parsed content */
	ExprTree  *parsed_node;	/* Parse tree with the expression */
	Value     *value;	/* Last value computed */
	Style     *style;
	
	/* computed versions of the cell contents */
	String    *text;	/* Text rendered and displayed */
	int       width;	/* Width of text */
	int       height;	/* Height of text */

	int       flags;
	char      generation;
} Cell;

typedef GList CellList;

#define CELL_TEXT_GET(cell) ((cell)->text ? cell->text->str : cell->entered_text->str)
#define CELL_IS_FORMULA(cell) (cell->entered_text->str [0] == '=')

typedef struct {
	int col_offset, row_offset; /* Position of the cell */
	Cell *cell;
} CellCopy;

typedef GList CellCopyList;

typedef struct {
	int            cols, rows;
	CellCopyList *list;
} CellRegion;

char       *value_format              (Value *value, StyleFormat *format, char **color);

void        cell_set_text             (Cell *cell, char *text);
void        cell_set_formula          (Cell *cell, char *text);
void        cell_set_format           (Cell *cell, char *format);
void        cell_set_font             (Cell *cell, char *font_name);
void        cell_set_font_from_style  (Cell *cell, StyleFont *style_font);
void        cell_set_alignment        (Cell *cell, int halign, int valign, int orientation, int auto_return);
void        cell_set_halign           (Cell *cell, StyleHAlignFlags halign);
void        cell_set_rendered_text    (Cell *cell, char *rendered_text);
void        cell_formula_relocate     (Cell *cell, int target_col, int target_row);
void        cell_get_span             (Cell *cell, int *col1, int *col2);
void        cell_make_value           (Cell *cell);
void        cell_render_value         (Cell *cell);
void        cell_calc_dimensions      (Cell *cell);
Cell       *cell_copy                 (Cell *cell);
void        cell_destroy              (Cell *cell);
void        cell_formula_changed      (Cell *cell);
void        cell_queue_redraw         (Cell *cell);
int         cell_get_horizontal_align (Cell *cell);

void        cell_draw                 (Cell *cell, void *sheet_view,
				       GdkGC *gc, GdkDrawable *drawable,
				       int x, int y);

void        calc_text_dimensions      (int is_number, Style *style, char *text,
				       int cell_w, int cell_h, int *h, int *w);

/*
 * Routines used to lookup which cells displays on a given column
 *
 * These are defined in cellspan.c
 */
Cell *      row_cell_get_displayed_at (ColRowInfo *ri, int col);
void        cell_register_span        (Cell *cell, int left, int right);
void        cell_unregister_span      (Cell *cell);

void        row_init_span             (ColRowInfo *ri);
void        row_destroy_span          (ColRowInfo *ri);

#endif /* GNUMERIC_CELL_H */

