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

	unsigned   int hard_size:1;     /* has the user explicitly set the dimensions? */
	void       *data;
} ColRowInfo;

#define COL_INTERNAL_WIDTH(col) ((col)->pixels - ((col)->margin_b + (col)->margin_a))
#define ROW_INTERNAL_HEIGHT(row) ((row)->pixels - ((row)->margin_b + (row)->margin_a))

#define CELL_ERROR       1
#define CELL_HAS_COMMENT 2
#define CELL_FORMAT_SET  4

/**
 * CellComment:
 *
 * Holds the comment string as well as the GnomeCanvasItem marker
 * that appears on the spreadsheet
 */
typedef struct {
	String          *comment;
	int             timer_tag;
	void            *window;
	
	/* A list of GnomeCanvasItems, one per SheetView */
	GList           *realized_list;
} CellComment;

/**
 * Cell:
 *
 * Definition of a Gnumeric Cell
 */
typedef struct {
	void        *sheet;
	ColRowInfo  *col;
	ColRowInfo  *row;

	/* Text as entered by the user */
	String      *entered_text;
	
	/* Type of the content and the actual parsed content */
	ExprTree    *parsed_node;	/* Parse tree with the expression */
	Value       *value;		/* Last value computed */
	Style       *style;		/* The Cell's style */

	StyleColor  *render_color;      /* If a manually entered color has been selected */

	/* computed versions of the cell contents */
	String      *text;	/* Text rendered and displayed */
	int         width;	/* Width of text */
	int         height;	/* Height of text */

	CellComment *comment;
	int         flags;
	char        generation;
} Cell;

typedef GList CellList;

/* #define CELL_TEXT_GET(cell)      ((cell)->text ? cell->text->str : cell->entered_text->str) */
#define CELL_IS_FORMAT_SET(cell) ((cell)->flags & CELL_FORMAT_SET)
#define CELL_IS_NUMBER(cell) (cell->value ? 				\
			      (cell->value->type == VALUE_FLOAT || 	\
			       cell->value->type == VALUE_INTEGER) 	\
			      : 0)

typedef enum {
	CELL_COPY_TYPE_CELL,
	CELL_COPY_TYPE_TEXT,
} CellCopyType;

typedef struct {
	int  col_offset, row_offset; /* Position of the cell */
	int  type;
	union {
		Cell *cell;
		char *text;
	} u;
} CellCopy;

typedef GList CellCopyList;

typedef struct {
	int          cols, rows;
	CellCopyList *list;
} CellRegion;

char       *value_format                 (Value *value, StyleFormat *format, char **color);

void        cell_set_text                (Cell *cell, char *text);
void        cell_set_text_simple         (Cell *cell, char *text);
void        cell_content_changed         (Cell *cell);
void        cell_set_formula             (Cell *cell, char *text);
void        cell_set_formula_tree        (Cell *cell, ExprTree *formula);
void        cell_set_formula_tree_simple (Cell *cell, ExprTree *formula);
void        cell_set_format              (Cell *cell, char *format);
void        cell_set_format_simple       (Cell *cell, char *format);
void        cell_set_font                (Cell *cell, char *font_name);
void        cell_set_style               (Cell *cell, Style *reference_style);
void        cell_set_comment             (Cell *cell, char *str);
void        cell_comment_destroy         (Cell *cell);
void        cell_comment_reposition      (Cell *cell);
void        cell_set_font_from_style     (Cell *cell, StyleFont *style_font);
void        cell_set_foreground          (Cell *cell, gushort red,
					  gushort green, gushort blue);
void        cell_set_background          (Cell *cell, gushort red,
					  gushort green, gushort blue);
void        cell_set_pattern             (Cell *cell, int pattern);
void        cell_set_border              (Cell *cell,
					  StyleBorderType border_type [4],
					  StyleColor *border_color [4]);
void        cell_set_alignment           (Cell *cell, int halign, int valign,
					  int orientation, int auto_return);
void        cell_set_halign              (Cell *cell, StyleHAlignFlags halign);
void        cell_set_rendered_text       (Cell *cell, char *rendered_text);
void        cell_relocate                (Cell *cell,
					  int col_diff, int row_diff);
void        cell_get_span                (Cell *cell, int *col1, int *col2);
char       *cell_get_text                (Cell *cell);
char       *cell_get_content             (Cell *cell);
char       *cell_get_value_as_text       (Cell *cell);
void        cell_make_value              (Cell *cell);
void        cell_render_value            (Cell *cell);
void        cell_calc_dimensions         (Cell *cell);
Cell       *cell_copy                    (Cell *cell);
void        cell_destroy                 (Cell *cell);
void        cell_formula_changed         (Cell *cell);
void        cell_queue_redraw            (Cell *cell);
int         cell_get_horizontal_align    (Cell *cell);

int         cell_draw                    (Cell *cell, void *sheet_view,
					  GdkGC *gc, GdkDrawable *drawable,
					  int x, int y);

void        calc_text_dimensions         (int is_number, Style *style, char *text,
					  int cell_w, int cell_h, int *h, int *w);

void        cell_realize                 (Cell *cell);
void        cell_unrealize               (Cell *cell);

/*
 * Optimizations to stop cell_queue_redraw to be invoked
 */
void        cell_thaw_redraws            (void);
void        cell_freeze_redraws          (void);

/*
 * Routines used to lookup which cells displays on a given column
 *
 * These are defined in cellspan.c
 */
Cell *      row_cell_get_displayed_at    (ColRowInfo *ri, int col);
void        cell_register_span           (Cell *cell, int left, int right);
void        cell_unregister_span         (Cell *cell);

void        row_init_span                (ColRowInfo *ri);
void        row_destroy_span             (ColRowInfo *ri);

#endif /* GNUMERIC_CELL_H */

