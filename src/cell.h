#ifndef GNUMERIC_CELL_H
#define GNUMERIC_CELL_H

#include <glib.h>
#include "gnumeric.h"
#include "dependent.h"

typedef enum {
    /* MUST BE > 0xffff for dependent */
    /* Cell has an expression rather than entered_text */
    CELL_HAS_EXPRESSION	   = 0x10000,

    /* Cell is linked into the sheet */
    CELL_IN_SHEET_LIST	   = 0x20000
} CellFlags;

typedef struct _CellComment CellComment;

/* Definition of a Gnumeric Cell */
#define DEP_TO_CELL(dep)	((Cell *)(dep))
#define CELL_TO_DEP(cell)	((Dependent *)(cell))
struct _Cell {
	Dependent base;
	/* Mandatory state information */
	CellPos	     pos;
	ColRowInfo  *col_info;
	ColRowInfo  *row_info;

	Value         *value;	/* computed or entered (Must be non NULL) */
	RenderedValue *rendered_value;

	/*  Only applies if the region has format general */
	StyleFormat *format;	/* Prefered format to render value */

	/* DEPRECATED : this field will be removed shortly */
	String      *entered_text;	/* Text as entered by the user. */

	CellComment *comment;
};

/**
 * Manage cells
 */
Cell       *cell_copy                    (Cell const *cell);
void        cell_destroy                 (Cell *cell);
void        cell_relocate                (Cell *cell, ExprRewriteInfo *rwinfo);
void        cell_eval_content            (Cell *cell);
void        cell_content_changed         (Cell *cell);

/**
 * Cell state checking
 */
gboolean    cell_is_blank		(Cell const * cell);
Value *     cell_is_error               (Cell const * cell);
gboolean    cell_is_number  		(Cell const * cell);
gboolean    cell_is_zero		(Cell const * cell);
ExprArray const * cell_is_array         (Cell const * cell);
gboolean    cell_is_partial_array       (Cell const * cell);
#define	    cell_needs_recalc(cell)	((cell)->base.flags & DEPENDENT_QUEUED_FOR_RECALC)
#define	    cell_expr_is_linked(cell)	((cell)->base.flags & DEPENDENT_IN_EXPR_LIST)
#define	    cell_has_expr(cell)		((cell)->base.flags & CELL_HAS_EXPRESSION)
#define	    cell_is_linked(cell)	((cell)->base.flags & CELL_IN_SHEET_LIST)
#define	    cell_has_comment(cell)	((cell)->comment != NULL)

/**
 * Utilities to assign the contents of a cell
 */
void        cell_set_text                (Cell *cell, char const *text);
void        cell_set_text_and_value      (Cell *cell, String *text,
					  Value *v, StyleFormat *opt_fmt);
void        cell_assign_value            (Cell *cell, Value *v, StyleFormat *opt_fmt);
void        cell_set_value               (Cell *cell,
					  Value *v, StyleFormat *opt_fmt);
void        cell_set_expr_and_value      (Cell *cell, ExprTree *expr, Value *v);
void        cell_set_expr                (Cell *cell, ExprTree *formula,
					  StyleFormat *opt_fmt);
void	    cell_set_expr_unsafe 	 (Cell *cell, ExprTree *expr,
					  StyleFormat *opt_fmt);
void        cell_set_array_formula       (Sheet *sheet, int rowa, int cola,
					  int rowb, int colb,
					  ExprTree *formula,
					  gboolean queue_recalc);

/**
 * Manipulate Cell attributes
 */
MStyle     *cell_get_mstyle              (Cell const *cell);
void        cell_set_mstyle              (Cell const *cell, MStyle *mstyle);

char *      cell_get_format              (Cell const *cell);
void        cell_set_format              (Cell *cell, char const *format);

void        cell_make_value              (Cell *cell);
void        cell_render_value            (Cell *cell);

#endif /* GNUMERIC_CELL_H */
