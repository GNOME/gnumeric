#ifndef GNUMERIC_CELL_H
#define GNUMERIC_CELL_H

#include <glib.h>
#include "gnumeric.h"

#include "style.h"
#include "str.h"

/* Cell contains a comment */
#define CELL_HAS_COMMENT       1

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

	String      *text;		/* Text rendered and displayed */
	String      *entered_text;	/* Text as entered by the user. */
	
	ExprTree    *parsed_node;	/* Parse tree with the expression */
	Value       *value;		/* Last value computed */

	/* Colour supplied by the formater eg [Red]0.00 */
	StyleColor  *render_color;

	/*
	 * Computed sizes of rendered text.
	 * In pixels EXCLUSIVE of margins and grid lines
	 */
	int         width_pixel, height_pixel;

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
		Cell   *cell;
		char *text;
	} u;
} CellCopy;

typedef GList CellCopyList;

struct _CellRegion {
	int          cols, rows;
	CellCopyList *list;
	GList        *styles;
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

void        cell_calculate_span          (Cell const * const cell,
					  int * const col1, int * const col2);
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
gboolean    cell_is_number  		 (const Cell *cell);
gboolean    cell_is_zero		 (const Cell *cell);

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

/* Cell state checking */
gboolean    cell_is_blank		 (Cell const * const cell);
Value *     cell_is_error                (Cell const * const cell);

char *      cell_get_format              (const Cell *cell);
gboolean    cell_has_assigned_format     (const Cell *cell);

#endif /* GNUMERIC_CELL_H */
