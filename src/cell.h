#ifndef GNUMERIC_CELL_H
#define GNUMERIC_CELL_H

#include <glib.h>
#include "gnumeric.h"
#include "style.h"
#include "sheet.h"
#include "sheet-view.h"
#include "str.h"
#include "expr.h"

struct _ColRowInfo {
	int        pos;			/* the column or row number */

	double     units;               /* In points */
	double     margin_a_pt;
	double     margin_b_pt;

	int        margin_a;  		/* in pixels: top/left margin */
	int        margin_b; 		/* in pixels: bottom/right margin */
	int        pixels;		/* we compute this from the above parameters */

	unsigned   int hard_size:1;     /* has the user explicitly set the dimensions? */

	/* TODO : Add per row/col min/max */

	void *spans;	/* Only used for rows */
};

struct _ColRowCollection
{
	int         max_used;
	ColRowInfo  default_style;
	GPtrArray * info;
};

#define COL_INTERNAL_WIDTH(col) ((col)->pixels - ((col)->margin_b + (col)->margin_a))
#define ROW_INTERNAL_HEIGHT(row) ((row)->pixels - ((row)->margin_b + (row)->margin_a))

/* Cell contains a comment */
#define CELL_HAS_COMMENT       1

/*
 * #define CELL_FORMAT_SET        2
 */

/* Cell has been queued for recalc */
#define CELL_QUEUED_FOR_RECALC 4

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
struct _Cell {
	Sheet       *sheet;
	ColRowInfo  *col;
	ColRowInfo  *row;

	/* Text as entered by the user.  This is only used for cells
	   with parse errors.  */
	String      *entered_text;
	
	/* Type of the content and the actual parsed content */
	ExprTree    *parsed_node;	/* Parse tree with the expression */
	Value       *value;		/* Last value computed */

	StyleColor  *render_color;      /* If a manually entered color has been selected */

	/* computed versions of the cell contents */
	String      *text;	/* Text rendered and displayed */
	int         width;	/* Width of text */
	int         height;	/* Height of text */

	CellComment *comment;
	int         flags;
	guint8      generation;
};

typedef enum {
	CELL_COPY_TYPE_CELL,
	CELL_COPY_TYPE_TEXT,
} CellCopyType;

typedef struct {
	int col_offset, row_offset; /* Position of the cell */
	guint8 type;
	union {
		struct {
			Cell   *cell;
			MStyle *mstyle;
		} cell;
		char *text;
	} u;
} CellCopy;

typedef GList CellCopyList;

struct _CellRegion {
	int          cols, rows;
	CellCopyList *list;
};

char       *value_format                 (Value *value, StyleFormat *format, char **color);

void        cell_set_text                (Cell *cell, const char *text);
void        cell_set_text_simple         (Cell *cell, const char *text);
void        cell_set_value               (Cell *cell, Value *v);
void        cell_set_value_simple        (Cell *cell, Value *v);
void        cell_content_changed         (Cell *cell);
void        cell_set_formula             (Cell *cell, const char *text);
void        cell_set_formula_tree        (Cell *cell, ExprTree *formula);
void        cell_set_formula_tree_simple (Cell *cell, ExprTree *formula);
void        cell_set_array_formula       (Sheet *sheet, int rowa, int cola,
					  int rowb, int colb,
					  ExprTree *formula);
void        cell_set_format              (Cell *cell, const char *format);
void        cell_set_format_simple       (Cell *cell, const char *format);
void        cell_set_format_from_style   (Cell *cell, StyleFormat *style_format);

void        cell_set_comment             (Cell *cell, const char *str);
void        cell_comment_destroy         (Cell *cell);
void        cell_comment_reposition      (Cell *cell);
char       *cell_get_comment             (Cell *cell);

void        cell_set_rendered_text       (Cell *cell, const char *rendered_text);
char *      cell_get_formatted_val       (Cell *cell, StyleColor **col);
MStyle     *cell_get_mstyle              (const Cell *cell);
void        cell_set_mstyle              (const Cell *cell, MStyle *mstyle);
void        cell_style_changed           (Cell *cell);
void        cell_relocate                (Cell *cell, gboolean const check_bounds);

void        cell_get_span                (Cell *cell, int *col1, int *col2);
char       *cell_get_text                (Cell *cell);
char       *cell_get_content             (Cell *cell);
char       *cell_get_value_as_text       (Cell *cell);
void        cell_make_value              (Cell *cell);
void        cell_render_value            (Cell *cell);
void        cell_calc_dimensions         (Cell *cell);
Cell       *cell_copy                    (const Cell *cell);
void        cell_destroy                 (Cell *cell);
void        cell_queue_redraw            (Cell *cell);
int         cell_get_horizontal_align    (const Cell *cell, int align);
int	    cell_is_number  		 (const Cell *cell);

int         cell_draw                    (Cell *cell, SheetView *sheet_view,
					  GdkGC *gc, GdkDrawable *drawable,
					  int x, int y);

void        cell_realize                 (Cell *cell);
void        cell_unrealize               (Cell *cell);

/*
 * Optimizations to stop cell_queue_redraw to be invoked
 */
void        cell_thaw_redraws            (void);
void        cell_freeze_redraws          (void);

/*
 * Optimizations to stop any queueing of redraws.
 */
void        cell_deep_thaw_redraws            (void);
void        cell_deep_freeze_redraws          (void);

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

/* A utility routine to check if a cell is blank */
gboolean    cell_is_blank		 (Cell *cell);

/* If a cell has an error value return it */
Value *     cell_is_error                 (Cell const *cell);

#endif /* GNUMERIC_CELL_H */
