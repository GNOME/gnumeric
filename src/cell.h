#ifndef GNUMERIC_CELL_H
#define GNUMERIC_CELL_H

#include <glib.h>
#include "gnumeric.h"

#include "style.h"
#include "str.h"

typedef enum {
    /* Cell has an expression rather than entered_text */
    CELL_HAS_EXPRESSION	   = 1,

    /* Cell has been queued for recalc */
    CELL_QUEUED_FOR_RECALC = 2
} CellFlags;

typedef struct _CellComment CellComment;

/* Definition of a Gnumeric Cell */
struct _Cell {
	/* Mandatory state information */
	CellFlags    cell_flags;
	Sheet       *sheet;
	ColRowInfo  *col_info;
	ColRowInfo  *row_info;

	Value         *value;	/* computed or entered (Must be non NULL) */
	RenderedValue *rendered_value;

	/*  Only applies if the region has format general */
	StyleFormat *format;	/* Prefered format to render value */

	union {
		ExprTree    *expression;	/* Parse tree with the expression */
		String      *entered_text;	/* Text as entered by the user. */
	} u; /* TODO : find a better naming scheme */

	CellComment *comment;
	guint8       generation;
};

/**
 * Manage cells
 */
Cell       *cell_copy                    (const Cell *cell);
void        cell_destroy                 (Cell *cell);
void        cell_content_changed         (Cell *cell);
void        cell_relocate                (Cell *cell, gboolean const check_bounds);

/**
 * Cell state checking
 */
gboolean    cell_is_blank		(Cell const * const cell);
Value *     cell_is_error               (Cell const * const cell);
gboolean    cell_is_number  		(Cell const * const cell);
gboolean    cell_is_zero		(Cell const * const cell);
ArrayRef const * cell_is_array          (Cell const * const cell);
gboolean    cell_is_partial_array       (Cell const * const cell);
#define	    cell_needs_recalc(cell)	((cell)->cell_flags & CELL_QUEUED_FOR_RECALC)
#define	    cell_has_expr(cell)		((cell)->cell_flags & CELL_HAS_EXPRESSION)
#define	    cell_has_comment(cell)	((cell)->comment != NULL)

/**
 * Utilities to assign the contents of a cell
 */
void        cell_set_text                (Cell *cell, const char *text);
void        cell_set_text_and_value      (Cell *cell, String *text,
					  Value *v, char const * optional_format);
void        cell_assign_value            (Cell *cell, Value *v, char const * optional_format);
void        cell_set_value               (Cell *cell,
					  Value *v, char const * optional_format);
void        cell_set_expr_and_value      (Cell *cell, ExprTree *expr, Value *v);
void        cell_set_expr                (Cell *cell, ExprTree *formula,
					  char const * optional_format);
void        cell_set_array_formula       (Sheet *sheet, int rowa, int cola,
					  int rowb, int colb,
					  ExprTree *formula,
					  gboolean const queue_recalc);

/**
 * Manipulate Cell attributes
 */
MStyle     *cell_get_mstyle              (const Cell *cell);
void        cell_set_mstyle              (const Cell *cell, MStyle *mstyle);

char *      cell_get_format              (const Cell *cell);
void        cell_set_format              (Cell *cell, const char *format);

void        cell_make_value              (Cell *cell);	/* FIXME : This is crap ?! */
void        cell_render_value            (Cell *cell);

#endif /* GNUMERIC_CELL_H */
