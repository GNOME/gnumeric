#ifndef GNUMERIC_CELL_H
#define GNUMERIC_CELL_H

typedef unsigned char  ColType;
typedef unsigned short RowType;

typedef struct {
	int       ref_count;
	GdkColor  color;
} CellColor;

typedef struct {
	int        pos;			/* the column or row number */

	/* The height */
	int        units;		/* In units */
	int        margin_a;  		/* in pixels: top/left margin */
	int        margin_b; 		/* in pixels: bottom/right margin */
	int        pixels;		/* we compute this from the above parameters */

	unsigned   int selected:1;	/* is this selected? */

	void       *data;
} ColRowInfo;

typedef enum {
	CELL_COLOR_IS_SET  = 1,

	/* If this flag is set we are free to change the style of a cell
	 * automatically depending on the type of data entered (strings
	 * get left alignment, numbers right alignment, etc).
	 *
	 * If it is not set, then we can do this.
	 */
	CELL_DEFAULT_STYLE = 2, 
} CellFlags;

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
	GdkColor  color;	/* color for the displayed text */
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
void        cell_set_alignment        (Cell *cell, int halign, int valign, int orientation);
void        cell_set_rendered_text    (Cell *cell, char *rendered_text);
void        cell_formula_relocate     (Cell *cell, int target_col, int target_row);
void        cell_make_value           (Cell *cell);
void        cell_render_value         (Cell *cell);
void        cell_calc_dimensions      (Cell *cell);
Cell       *cell_copy                 (Cell *cell);
void        cell_destroy              (Cell *cell);
void        cell_formula_changed      (Cell *cell);
void        cell_queue_redraw         (Cell *cell);
#endif /* GNUMERIC_CELL_H */

